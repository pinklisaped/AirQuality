#pragma once

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/uart.h>
#include <sdkconfig.h>

// Common Settings
#define SCD_I2C_FREQ_HZ 100000 // I2C frequency
#define PM_UART_BAUDRATE 9600
#define LOG_LEVEL ESP_LOG_INFO

#ifdef CONFIG_IDF_TARGET_ESP32C6

#ifdef CONFIG_USE_PMS5003
// PM Sensor (PMS5003 UART)
#define PM_UART_PORT UART_NUM_1
#define PM_UART_RX_IO GPIO_NUM_4
#define PM_UART_TX_IO GPIO_NUM_5
#else
// PM Sensor (PMS5003 I2C)
#define PM_I2C_PORT I2C_NUM_1
#define PM_I2C_SCL_IO GPIO_NUM_4
#define PM_I2C_SDA_IO GPIO_NUM_5
#endif

// I2C SCD4x configuration
#define SCD_I2C_PORT I2C_NUM_0    // I2C port number
#define SCD_I2C_SCL_IO GPIO_NUM_6 // GPIO for SCL
#define SCD_I2C_SDA_IO GPIO_NUM_7 // GPIO for SDA

#elifdef CONFIG_IDF_TARGET_ESP32H2
#ifdef CONFIG_USE_PMS5003

// PM Sensor (PMS5003 UART)
#define PM_UART_PORT UART_NUM_1
#define PM_UART_RX_IO GPIO_NUM_0
#define PM_UART_TX_IO GPIO_NUM_1

#elifdef CONFIG_USE_SPS30

// PM Sensor (PMS5003 I2C)
#define PM_I2C_PORT I2C_NUM_1
#define PM_I2C_SCL_IO GPIO_NUM_0
#define PM_I2C_SDA_IO GPIO_NUM_1

#endif

#define LED_PIN GPIO_NUM_8
#define ZIGBEE_BUTTON_PIN GPIO_NUM_5

// I2C SCD4x configuration
#define SCD_I2C_PORT I2C_NUM_0    // I2C port number
#define SCD_I2C_SCL_IO GPIO_NUM_2 // GPIO for SCL
#define SCD_I2C_SDA_IO GPIO_NUM_3 // GPIO for SDA
#define LIGHT_LEVEL_PIN GPIO_NUM_4

// Display Configuration
#define LCD_PIN_SCLK GPIO_NUM_14
#define LCD_PIN_MOSI GPIO_NUM_13
#define LCD_PIN_CS GPIO_NUM_10
#define LCD_PIN_DC GPIO_NUM_11
#define LCD_PIN_RST GPIO_NUM_9
#define LCD_PIN_BK_LIGHT GPIO_NUM_12

#endif

// Display Configuration
#define LCD_H_RES 240
#define LCD_V_RES 240
#define LCD_DRAW_BUF_HEIGHT 10
#define LCD_SPI_HOST SPI2_HOST
