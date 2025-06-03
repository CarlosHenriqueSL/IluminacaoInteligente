# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(IluminacaoInteligente C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(IluminacaoInteligente IluminacaoInteligente.c lib/porcentagens.c lib/ssd1306.c)

pico_set_program_name(IluminacaoInteligente "IluminacaoInteligente")
pico_set_program_version(IluminacaoInteligente "0.1")

pico_generate_pio_header(IluminacaoInteligente ${CMAKE_CURRENT_LIST_DIR}/blink.pio)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(IluminacaoInteligente 0)
pico_enable_stdio_usb(IluminacaoInteligente 1)

# Add the standard library to the build
target_link_libraries(IluminacaoInteligente
    pico_stdlib
    hardware_i2c
    hardware_pio
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_mqtt
    pico_mbedtls
    pico_lwip_mbedtls
    )

include_directories(${CMAKE_SOURCE_DIR}/lib)

# Add the standard include files to the build
target_include_directories(IluminacaoInteligente PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Add any user requested libraries
target_link_libraries(IluminacaoInteligente 
        
        )

pico_add_extra_outputs(IluminacaoInteligente)

