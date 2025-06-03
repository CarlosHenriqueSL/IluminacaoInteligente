#include "pico/stdlib.h"     // Biblioteca da Raspberry Pi Pico para funções padrão (GPIO, temporização, etc.)
#include "pico/cyw43_arch.h" // Biblioteca para arquitetura Wi-Fi da Pico com CYW43
#include "pico/unique_id.h"  // Biblioteca com recursos para trabalhar com os pinos GPIO do Raspberry Pi Pico

#include "hardware/pio.h"        // PIO para a matriz de LEDs
#include "hardware/i2c.h"        // I2C para o display ssd1306
#include "hardware/clocks.h"     // Clocks para o arquivo blink.pio
#include "blink.pio.h"           // Arquivo em assembly para comunicacao com a matriz
#include "lib/porcentagens.h"    // Arquivo da pasta lib/ que contem os padroes que aparecerao na matriz
#include "lib/ssd1306.h"         // Arquivo da pasta lib/ para escrever no display

#include "lwip/apps/mqtt.h"      // Biblioteca LWIP MQTT - fornece funções e recursos para conexão MQTT
#include "lwip/apps/mqtt_priv.h" // Biblioteca que fornece funções e recursos para Geração de Conexões
#include "lwip/dns.h"            // Biblioteca que fornece funções e recursos suporte DNS:
#include "lwip/altcp_tls.h"      // Biblioteca que fornece funções e recursos para conexões seguras usando TLS:

#define WIFI_SSID "" // Substitua pelo nome da sua rede Wi-Fi
#define WIFI_PASSWORD ""       // Substitua pela senha da sua rede Wi-Fi
#define MQTT_SERVER ""      // Substitua pelo endereço do host - broket MQTT: Ex: 192.168.1.107
#define MQTT_USERNAME ""         // Substitua pelo nome da host MQTT - Username
#define MQTT_PASSWORD ""           // Substitua pelo Password da host MQTT - credencial de acesso - caso exista

#define WS2812_PIN 7
#define LED_PIN_GREEN 11
#define LED_PIN_RED 13

#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define ENDERECO 0x3C

ssd1306_t ssd;

PIO g_pio;
uint g_sm;

// Definição dos tópicos. Luminosidade para cada comodo
#define MQTT_TOPIC_SALA_NIVEL "/luminosidade/sala"
#define MQTT_TOPIC_QUARTO1_NIVEL "/luminosidade/quarto1"
#define MQTT_TOPIC_QUARTO2_NIVEL "/luminosidade/quarto2"
#define MQTT_TOPIC_COZINHA_NIVEL "/luminosidade/cozinha"
#define MQTT_TOPIC_BANHEIRO_NIVEL "/luminosidade/banheiro"

#ifndef MQTT_SERVER
#error Need to define MQTT_SERVER
#endif

// This file includes your client certificate for client server authentication
#ifdef MQTT_CERT_INC
#include MQTT_CERT_INC
#endif

#ifndef MQTT_TOPIC_LEN
#define MQTT_TOPIC_LEN 100
#endif

// Dados do cliente MQTT
typedef struct
{
    mqtt_client_t *mqtt_client_inst;
    struct mqtt_connect_client_info_t mqtt_client_info;
    char data[MQTT_OUTPUT_RINGBUF_SIZE];
    char topic[MQTT_TOPIC_LEN];
    uint32_t len;
    ip_addr_t mqtt_server_address;
    bool connect_done;
    int subscribe_count;
    bool stop_client;
} MQTT_CLIENT_DATA_T;

#ifndef DEBUG_printf
#ifndef NDEBUG
#define DEBUG_printf printf
#else
#define DEBUG_printf(...)
#endif
#endif

#ifndef INFO_printf
#define INFO_printf printf
#endif

#ifndef ERROR_printf
#define ERROR_printf printf
#endif

// Manter o programa ativo - keep alive in seconds
#define MQTT_KEEP_ALIVE_S 60

// QoS - mqtt_subscribe
// At most once (QoS 0)
// At least once (QoS 1)
// Exactly once (QoS 2)
#define MQTT_SUBSCRIBE_QOS 1
#define MQTT_PUBLISH_QOS 1
#define MQTT_PUBLISH_RETAIN 0

// Tópico usado para: last will and testament
#define MQTT_WILL_TOPIC "/online"
#define MQTT_WILL_MSG "0"
#define MQTT_WILL_QOS 1

#ifndef MQTT_DEVICE_NAME
#define MQTT_DEVICE_NAME "pico"
#endif

// Definir como 1 para adicionar o nome do cliente aos tópicos, para suportar vários dispositivos que utilizam o mesmo servidor
#ifndef MQTT_UNIQUE_TOPIC
#define MQTT_UNIQUE_TOPIC 0
#endif

/* References for this implementation:
 * raspberry-pi-pico-c-sdk.pdf, Section '4.1.1. hardware_adc'
 * pico-examples/adc/adc_console/adc_console.c */

// Cabecalhos das funcoes

uint32_t matrix_rgb(double r, double g, double b);
void desenho_pio(double *desenho, PIO pio, uint sm, float r, float g, float b);
static void pub_request_cb(__unused void *arg, err_t err);
static const char *full_topic(MQTT_CLIENT_DATA_T *state, const char *name);
static void control_led(MQTT_CLIENT_DATA_T *state, bool on);
static void sub_request_cb(void *arg, err_t err);
static void unsub_request_cb(void *arg, err_t err);
static void sub_unsub_topics(MQTT_CLIENT_DATA_T *state, bool sub);
static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags);
static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len);
static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status);
static void start_client(MQTT_CLIENT_DATA_T *state);
static void dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg);

int main(void)
{
    stdio_init_all();

    i2c_init(I2C_PORT, 400 * 1000);

    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, ENDERECO, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_send_data(&ssd);
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);

    g_pio = pio0;
    uint offset = pio_add_program(g_pio, &blink_program);
    g_sm = pio_claim_unused_sm(g_pio, true);
    blink_program_init(g_pio, g_sm, offset, WS2812_PIN);

    gpio_init(LED_PIN_GREEN);
    gpio_set_dir(LED_PIN_GREEN, GPIO_OUT);
    gpio_init(LED_PIN_RED);
    gpio_set_dir(LED_PIN_RED, GPIO_OUT);

    INFO_printf("mqtt client starting\n");
    gpio_put(LED_PIN_GREEN, 1);
    gpio_put(LED_PIN_RED, 1);

    static MQTT_CLIENT_DATA_T state;

    if (cyw43_arch_init())
    {
        panic("Failed to inizialize CYW43");
        gpio_put(LED_PIN_GREEN, 0);
    }

    char unique_id_buf[5];
    pico_get_unique_board_id_string(unique_id_buf, sizeof(unique_id_buf));
    for (int i = 0; i < sizeof(unique_id_buf) - 1; i++)
    {
        unique_id_buf[i] = tolower(unique_id_buf[i]);
    }

    char client_id_buf[sizeof(MQTT_DEVICE_NAME) + sizeof(unique_id_buf) - 1];
    memcpy(&client_id_buf[0], MQTT_DEVICE_NAME, sizeof(MQTT_DEVICE_NAME) - 1);
    memcpy(&client_id_buf[sizeof(MQTT_DEVICE_NAME) - 1], unique_id_buf, sizeof(unique_id_buf) - 1);
    client_id_buf[sizeof(client_id_buf) - 1] = 0;
    INFO_printf("Device name %s\n", client_id_buf);

    state.mqtt_client_info.client_id = client_id_buf;
    state.mqtt_client_info.keep_alive = MQTT_KEEP_ALIVE_S;
#if defined(MQTT_USERNAME) && defined(MQTT_PASSWORD)
    state.mqtt_client_info.client_user = MQTT_USERNAME;
    state.mqtt_client_info.client_pass = MQTT_PASSWORD;
#else
    state.mqtt_client_info.client_user = NULL;
    state.mqtt_client_info.client_pass = NULL;
#endif
    static char will_topic[MQTT_TOPIC_LEN];
    strncpy(will_topic, full_topic(&state, MQTT_WILL_TOPIC), sizeof(will_topic));
    state.mqtt_client_info.will_topic = will_topic;
    state.mqtt_client_info.will_msg = MQTT_WILL_MSG;
    state.mqtt_client_info.will_qos = MQTT_WILL_QOS;
    state.mqtt_client_info.will_retain = true;
#if LWIP_ALTCP && LWIP_ALTCP_TLS
#ifdef MQTT_CERT_INC
    static const uint8_t ca_cert[] = TLS_ROOT_CERT;
    static const uint8_t client_key[] = TLS_CLIENT_KEY;
    static const uint8_t client_cert[] = TLS_CLIENT_CERT;
    state.mqtt_client_info.tls_config = altcp_tls_create_config_client_2wayauth(ca_cert, sizeof(ca_cert),
                                                                                client_key, sizeof(client_key), NULL, 0, client_cert, sizeof(client_cert));
#if ALTCP_MBEDTLS_AUTHMODE != MBEDTLS_SSL_VERIFY_REQUIRED
    WARN_printf("Warning: tls without verification is insecure\n");
#endif
#else
    state.mqtt_client_info.tls_config = altcp_tls_create_config_client(NULL, 0); // Corrigido state-> para state.
    WARN_printf("Warning: tls without a certificate is insecure\n");
#endif
#endif

    cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000))
    {
        panic("Failed to connect");
        gpio_put(LED_PIN_GREEN, 0);
    }
    INFO_printf("\nConnected to Wifi\n");

    cyw43_arch_lwip_begin();
    int err = dns_gethostbyname(MQTT_SERVER, &state.mqtt_server_address, dns_found, &state);
    cyw43_arch_lwip_end();

    if (err == ERR_OK)
    {
        start_client(&state);
    }
    else if (err != ERR_INPROGRESS)
    {
        panic("dns request failed");
        gpio_put(LED_PIN_GREEN, 0);
    }

    gpio_put(LED_PIN_RED, 0);

    while (!state.connect_done || mqtt_client_is_connected(state.mqtt_client_inst))
    {
        cyw43_arch_poll();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(10000));
    }

    gpio_put(LED_PIN_RED, 1);
    gpio_put(LED_PIN_GREEN, 0);
    INFO_printf("mqtt client exiting\n");
    return 0;
}

static void pub_request_cb(__unused void *arg, err_t err)
{
    if (err != 0)
    {
        ERROR_printf("pub_request_cb failed %d", err);
    }
}

static const char *full_topic(MQTT_CLIENT_DATA_T *state, const char *name)
{
#if MQTT_UNIQUE_TOPIC
    static char full_topic_buf[MQTT_TOPIC_LEN]; // Renomeado para evitar conflito com parâmetro
    snprintf(full_topic_buf, sizeof(full_topic_buf), "/%s%s", state->mqtt_client_info.client_id, name);
    return full_topic_buf;
#else
    return name;
#endif
}

// Define as cores da funcao desenho_pio
uint32_t matrix_rgb(double r, double g, double b)
{
    unsigned char R = r * 255;
    unsigned char G = g * 255;
    unsigned char B = b * 255;
    return ((uint32_t)(G) << 24) | ((uint32_t)(R) << 16) | ((uint32_t)(B) << 8);
}

// Funcao para desenhar na matriz de LEDs
void desenho_pio(double *desenho, PIO pio, uint sm, float r, float g, float b)
{
    for (int i = 0; i < NUM_PIXELS; i++)
    {
        pio_sm_put_blocking(pio, sm, matrix_rgb(desenho[NUM_PIXELS - 1 - i] * r, desenho[NUM_PIXELS - 1 - i] * g, desenho[NUM_PIXELS - 1 - i] * b));
    }
}

static void control_led(MQTT_CLIENT_DATA_T *state, bool on)
{
    const char *message = on ? "On" : "Off";
    if (on)
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    else
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
    mqtt_publish(state->mqtt_client_inst, full_topic(state, "/led/state"), message, strlen(message), MQTT_PUBLISH_QOS, MQTT_PUBLISH_RETAIN, pub_request_cb, state);
}

static void sub_request_cb(void *arg, err_t err)
{
    MQTT_CLIENT_DATA_T *state = (MQTT_CLIENT_DATA_T *)arg;
    if (err != 0)
    {
        panic("subscribe request failed %d", err);
    }
    state->subscribe_count++;
}

static void unsub_request_cb(void *arg, err_t err)
{
    MQTT_CLIENT_DATA_T *state = (MQTT_CLIENT_DATA_T *)arg;
    if (err != 0)
    {
        panic("unsubscribe request failed %d", err);
    }
    state->subscribe_count--;
    assert(state->subscribe_count >= 0);
    if (state->subscribe_count <= 0 && state->stop_client)
    {
        mqtt_disconnect(state->mqtt_client_inst);
    }
}

static void sub_unsub_topics(MQTT_CLIENT_DATA_T *state, bool sub)
{
    mqtt_request_cb_t cb = sub ? sub_request_cb : unsub_request_cb;
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "/led"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "/print"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "/ping"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "/exit"), MQTT_SUBSCRIBE_QOS, cb, state, sub);


    // Inscrições para os tópicos de nível de luminosidade de cada cômodo
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, MQTT_TOPIC_SALA_NIVEL), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, MQTT_TOPIC_QUARTO1_NIVEL), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, MQTT_TOPIC_QUARTO2_NIVEL), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, MQTT_TOPIC_COZINHA_NIVEL), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, MQTT_TOPIC_BANHEIRO_NIVEL), MQTT_SUBSCRIBE_QOS, cb, state, sub);
}

static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
    MQTT_CLIENT_DATA_T *state = (MQTT_CLIENT_DATA_T *)arg;
#if MQTT_UNIQUE_TOPIC
    const char *basic_topic = state->topic + strlen(state->mqtt_client_info.client_id) + 1;
#else
    const char *basic_topic = state->topic;
#endif
    strncpy(state->data, (const char *)data, len);
    state->len = len;
    state->data[len] = '\0';

    DEBUG_printf("Topic: %s, Message: %s\n", state->topic, state->data);

    if (strcmp(basic_topic, "/led") == 0)
    {
        if (lwip_stricmp((const char *)state->data, "On") == 0 || strcmp((const char *)state->data, "1") == 0)
            control_led(state, true);
        else if (lwip_stricmp((const char *)state->data, "Off") == 0 || strcmp((const char *)state->data, "0") == 0)
            control_led(state, false);
    }
    else if (strcmp(basic_topic, "/print") == 0)
    {
        INFO_printf("%.*s\n", len, data);
    }
    else if (strcmp(basic_topic, "/ping") == 0)
    {
        char buf[11];
        snprintf(buf, sizeof(buf), "%u", to_ms_since_boot(get_absolute_time()) / 1000);
        mqtt_publish(state->mqtt_client_inst, full_topic(state, "/uptime"), buf, strlen(buf), MQTT_PUBLISH_QOS, MQTT_PUBLISH_RETAIN, pub_request_cb, state);
    }
    else if (strcmp(basic_topic, "/exit") == 0)
    {
        state->stop_client = true;
        sub_unsub_topics(state, false);
    }
    
    // Condicional adicionada para verificar os niveis de cada comodo
    else if (strcmp(basic_topic, MQTT_TOPIC_SALA_NIVEL) == 0 ||
             strcmp(basic_topic, MQTT_TOPIC_QUARTO1_NIVEL) == 0 ||
             strcmp(basic_topic, MQTT_TOPIC_QUARTO2_NIVEL) == 0 ||
             strcmp(basic_topic, MQTT_TOPIC_COZINHA_NIVEL) == 0 ||
             strcmp(basic_topic, MQTT_TOPIC_BANHEIRO_NIVEL) == 0)
    {

        int nivel = atoi(state->data); // Converte a string recebida para inteiro

        double *desenho_atual = zeroPorcento; // Padrão para 0% ou valor inválido

        // Define a porcentagem que sera desenhada na matriz de LEDs
        if (nivel <= 20)
        {
            desenho_atual = zeroPorcento;
        }
        else if (nivel <= 40)
        {
            desenho_atual = vintePorcento;
        }
        else if (nivel <= 60)
        {
            desenho_atual = quarentaPorcento;
        }
        else if (nivel <= 80)
        {
            desenho_atual = sessentaPorcento;
        }
        else if (nivel < 95)
        {
            desenho_atual = oitentaPorcento;
        }
        else
        {
            desenho_atual = cemPorcento;
        }

        float cor_r = 0.5f; // Vermelho
        float cor_g = 0.5f; // Verde
        float cor_b = 0.5f; // Azul

        // Desenha a porcentagem na matriz de LEDs, usando a cor branca
        desenho_pio(desenho_atual, g_pio, g_sm, cor_r, cor_g, cor_b);

        // Lógica para escrever no display
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "Comodo:  ", 5, 16); 
        ssd1306_draw_string(&ssd, "Nivel:", 5, 40);

        // Buffer para formatar o nível de luminosidade
        char nivel_str[5];
        snprintf(nivel_str, sizeof(nivel_str), "%d%%", nivel);
        ssd1306_draw_string(&ssd, nivel_str, 58, 40);          // Escreve o nível

        // Compara o tópico para saber qual nome de cômodo escrever
        if (strcmp(basic_topic, MQTT_TOPIC_SALA_NIVEL) == 0)
        {
            ssd1306_draw_string(&ssd, "Sala", 62, 16);
        }
        else if (strcmp(basic_topic, MQTT_TOPIC_QUARTO1_NIVEL) == 0)
        {
            ssd1306_draw_string(&ssd, "Quarto 1", 62, 16);
        }
        else if (strcmp(basic_topic, MQTT_TOPIC_QUARTO2_NIVEL) == 0)
        {
            ssd1306_draw_string(&ssd, "Quarto 2", 62, 16);
        }
        else if (strcmp(basic_topic, MQTT_TOPIC_COZINHA_NIVEL) == 0)
        {
            ssd1306_draw_string(&ssd, "Cozinha", 62, 16);
        }
        else if (strcmp(basic_topic, MQTT_TOPIC_BANHEIRO_NIVEL) == 0)
        {
            ssd1306_draw_string(&ssd, "Banheiro", 62, 16);
        }

        ssd1306_rect(&ssd, 2, 2, 126, 62, true, false);
        ssd1306_send_data(&ssd);
    }
}

static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len)
{
    MQTT_CLIENT_DATA_T *state = (MQTT_CLIENT_DATA_T *)arg;
    strncpy(state->topic, topic, sizeof(state->topic));
    state->topic[sizeof(state->topic) - 1] = '\0';
}

static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
    MQTT_CLIENT_DATA_T *state = (MQTT_CLIENT_DATA_T *)arg;
    if (status == MQTT_CONNECT_ACCEPTED)
    {
        state->connect_done = true;
        sub_unsub_topics(state, true);
        if (state->mqtt_client_info.will_topic)
        {
            mqtt_publish(state->mqtt_client_inst, state->mqtt_client_info.will_topic, "1", 1, MQTT_WILL_QOS, true, pub_request_cb, state);
        }
    }
    else if (status == MQTT_CONNECT_DISCONNECTED)
    {
        if (!state->connect_done)
        {
            panic("Failed to connect to mqtt server");
        }
    }
    else
    {
        ERROR_printf("MQTT Connection status: %d\n", status);
    }
}

static void start_client(MQTT_CLIENT_DATA_T *state)
{
#if LWIP_ALTCP && LWIP_ALTCP_TLS
    const int port = MQTT_TLS_PORT;
    INFO_printf("Using TLS\n");
#else
    const int port = MQTT_PORT;
    INFO_printf("Warning: Not using TLS\n");
#endif

    state->mqtt_client_inst = mqtt_client_new();
    if (!state->mqtt_client_inst)
    {
        panic("MQTT client instance creation error");
    }
    INFO_printf("Connecting to mqtt server at %s port %d\n", ipaddr_ntoa(&state->mqtt_server_address), port);

    cyw43_arch_lwip_begin();
    err_t err = mqtt_client_connect(state->mqtt_client_inst, &state->mqtt_server_address, port, mqtt_connection_cb, state, &state->mqtt_client_info);
    if (err != ERR_OK)
    {
        ERROR_printf("MQTT broker connection error: %d\n", err);
    }
#if LWIP_ALTCP && LWIP_ALTCP_TLS
    if (state->mqtt_client_inst->conn)
    {
        mbedtls_ssl_set_hostname(altcp_tls_context(state->mqtt_client_inst->conn), MQTT_SERVER);
    }
#endif
    mqtt_set_inpub_callback(state->mqtt_client_inst, mqtt_incoming_publish_cb, mqtt_incoming_data_cb, state);
    cyw43_arch_lwip_end();
}

static void dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg)
{
    MQTT_CLIENT_DATA_T *state = (MQTT_CLIENT_DATA_T *)arg;
    if (ipaddr)
    {
        gpio_put(LED_PIN_GREEN, 1);
        gpio_put(LED_PIN_RED, 0);
        state->mqtt_server_address = *ipaddr;
        INFO_printf("DNS found for %s: %s\n", hostname, ipaddr_ntoa(ipaddr));
        start_client(state);
    }
    else
    {
        gpio_put(LED_PIN_GREEN, 0);
        gpio_put(LED_PIN_RED, 1);
        ERROR_printf("dns request failed for %s\n", hostname);
    }
}