# Iluminação Inteligente com BitDogLab e MQTT

## Visão Geral

Este projeto de automação residencial simula o controle de iluminação de cinco cômodos de uma casa (Sala, Quarto 1, Quarto 2, Cozinha e Banheiro) utilizando a placa BitDogLab e o protocolo MQTT para comunicação. O ajuste da intensidade luminosa de cada cômodo é realizado através de sliders no aplicativo "IoT MQTT Panel", e o feedback visual é fornecido pelos LEDs RGB, display OLED e pela matriz de LEDs.

## Autor
* Carlos Henrique Silva Lopes

## Objetivo Geral

Demonstrar um sistema de automação residencial para controle de iluminação, utilizando a placa BitDogLab e o protocolo MQTT para comunicação e ajuste da intensidade luminosa dos cômodos.

## Funcionalidades Principais

* **Controle Individual de Iluminação:** Ajuste da intensidade luminosa (0-100%) para 5 cômodos distintos.
* **Comunicação MQTT:** Utilização do protocolo MQTT para enviar e receber dados de controle.
* **Feedback Visual:**
    * **LEDs RGB:** Indicam o status da conexão com o broker MQTT (Amarelo: tentando conectar, Verde: conectado, Vermelho: falha na conexão).
    * **Display OLED:** Exibe o cômodo atualmente selecionado e sua respectiva porcentagem de iluminação.
    * **Matriz de LEDs:** Representa visualmente a intensidade da iluminação do cômodo atual em 5 níveis.
* **Interface de Controle:** Utilização do aplicativo "IoT MQTT Panel" para interagir com o sistema.
* **Broker MQTT Local:** Utilização do aplicativo Termux (com Mosquitto) para hospedar o broker MQTT.

## Componentes Utilizados

### Hardware
* Placa BitDogLab (com Raspberry Pi Pico W)
* LEDs RGB (verde e vermelho)
* Display SSD1306
* Matriz de LEDs WS2812

### Software e Ferramentas
* Raspberry Pi Pico C/C++ SDK
* Broker MQTT (Mosquitto rodando no Termux)
* Aplicativo IoT MQTT Panel

## Configuração do Ambiente

### 1. Broker MQTT

Para o broker MQTT, foi utilizado o Mosquitto no aplicativo Termux (Android):
1.  Instale o Termux.
2.  No Termux, instale o Mosquitto:
    ```bash
    pkg update && pkg upgrade
    pkg install mosquitto
    ```
3.  Inicie o broker Mosquitto:
    ```bash
    mosquitto -c /data/data/com.termux/files/usr/etc/mosquitto/mosquitto.conf
    ```
    (Ou simplesmente `mosquitto` se a configuração padrão for suficiente).
4.  Anote o endereço IP do seu dispositivo Android na rede Wi-Fi. Este será o `MQTT_SERVER` no código.

### 2. Firmware (Código do Projeto)

1.  **Clonar o Repositório:**
    ```bash
    git clone https://github.com/CarlosHenriqueSL/IluminacaoInteligente.git
    cd https://github.com/CarlosHenriqueSL/IluminacaoInteligente.git
    ```
2.  **Configurar Credenciais:** Abra o arquivo principal do código (provavelmente `main.c` ou similar) e edite as seguintes macros com suas informações:
    ```c
    #define WIFI_SSID "SUA_REDE_WIFI"
    #define WIFI_PASSWORD "SUA_SENHA_WIFI"
    #define MQTT_SERVER "IP_DO_SEU_BROKER_MQTT" // Ex: "192.168.1.107"
    #define MQTT_USERNAME "SEU_USUARIO_MQTT"    // Deixe "" se não houver usuário
    #define MQTT_PASSWORD "SUA_SENHA_MQTT"      // Deixe "" se não houver senha
    ```
3.  **Compilar e Gravar:**
    * Certifique-se de ter o ambiente de desenvolvimento para o Raspberry Pi Pico C/C++ SDK configurado.
    * Compile o projeto (geralmente usando `cmake` e `make`).
        ```bash
        mkdir build
        cd build
        cmake ..
        make
        ```
    * Coloque sua placa BitDogLab/Pico em modo BOOTSEL.
    * Arraste o arquivo `.uf2` gerado na pasta `build` para a unidade RPI-RP2 que aparece no seu computador.

### 3. Aplicativo de Controle (IoT MQTT Panel)

1.  Instale o aplicativo "IoT MQTT Panel" no seu smartphone ou tablet.
2.  **Configure a Conexão com o Broker:**
    * Adicione um novo broker.
    * **Broker Hostname/IP:** Insira o mesmo endereço IP que você configurou em `MQTT_SERVER`.
    * **Port:** Geralmente 1883 (padrão MQTT).
    * **Username/Password:** Insira as mesmas credenciais configuradas em `MQTT_USERNAME` e `MQTT_PASSWORD` (se houver).
3.  **Adicione os Controles (Sliders):**
    Crie 5 painéis do tipo "Slider" com as seguintes configurações de tópico de publicação (`Publish topic`):
    * **Sala:** `/luminosidade/sala` (Range: 0-100)
    * **Quarto 1:** `/luminosidade/quarto1` (Range: 0-100)
    * **Quarto 2:** `/luminosidade/quarto2` (Range: 0-100)
    * **Cozinha:** `/luminosidade/cozinha` (Range: 0-100)
    * **Banheiro:** `/luminosidade/banheiro` (Range: 0-100)

## Tópicos MQTT Utilizados

O sistema se inscreve nos seguintes tópicos para receber os níveis de luminosidade:

* `/luminosidade/sala`
* `/luminosidade/quarto1`
* `/luminosidade/quarto2`
* `/luminosidade/cozinha`
* `/luminosidade/banheiro`

## Funcionamento Detalhado

1.  **Inicialização e Conexão:**
    * Ao ser energizada, a placa BitDogLab tenta se conectar à rede Wi-Fi especificada.
    * Em seguida, tenta se conectar ao broker MQTT.
    * **LED Amarelo (Verde e Vermelho acesos simultaneamente):** Tentando conectar ao broker.
    * **LED Verde:** Conexão com o broker MQTT bem-sucedida.
    * **LED Vermelho:** Erro na conexão Wi-Fi ou com o broker MQTT.

2.  **Controle de Iluminação:**
    * No aplicativo "IoT MQTT Panel", ao mover o slider correspondente a um cômodo, uma mensagem MQTT é publicada no respectivo tópico (ex: `/luminosidade/sala`) com o valor da porcentagem (0-100).

3.  **Feedback na Placa:**
    * **Display OLED:** Ao receber uma nova mensagem MQTT, o display é atualizado para mostrar:
        * `Comodo: [Nome do Comodo]`
        * `Nivel: [Valor]%`
    * **Matriz de LEDs:** A matriz de LEDs é atualizada para representar a porcentagem de iluminação do cômodo atual:
        * **0-20%:** Matriz apagada.
        * **21-40%:** 1 linha da matriz acesa (cor branca).
        * **41-60%:** 2 linhas da matriz acesas.
        * **61-80%:** 3 linhas da matriz acesas.
        * **81-95%:** 4 linhas da matriz acesas.
        * **>95% (96-100%):** 5 linhas da matriz acesas.

## Estrutura do Código (Principais Arquivos e Diretórios)

* `IluminacaoInteligente.c`: Contém a lógica principal do programa, inicialização do Wi-Fi, callbacks MQTT e controle dos periféricos (LEDs RGB, Display OLED, Matriz de LEDs).
* `lib/porcentagens.h`: Contém os padrões (arrays) que definem quais LEDs acender na matriz para cada nível de porcentagem.
* `lib/ssd1306.h`: Biblioteca para controle do display OLED SSD1306.
* `blink.pio.h` e `blink.pio`: Arquivos gerados pelo `pioasm` para controle da matriz de LEDs WS2812 via PIO.
