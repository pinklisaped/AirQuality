// pms5003.cpp
#include "pms5003.h"

#ifdef CONFIG_USE_PMS5003

#include "settings.h"
#include <driver/uart.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

namespace PMS5003
{
bool isMeasuring = false;
static pms5003data storage = {};
pms5003data *lastData = nullptr;

static const char *TAG = "PMS5003";

static const uint8_t passiveModeCmd[] = {0x42, 0x4D, 0xE1, 0x00, 0x00, 0x01, 0x70};
static const uint8_t requestReadCmd[] = {0x42, 0x4D, 0xE2, 0x00, 0x00, 0x01, 0x71};
static const uint8_t pmsSleepCmd[] = {0x42, 0x4D, 0xE4, 0x00, 0x00, 0x01, 0x73};
static const uint8_t pmsWakeCmd[] = {0x42, 0x4D, 0xE4, 0x00, 0x01, 0x01, 0x74};

static void flushInput()
{
    uint8_t tmp[128];
    while (uart_read_bytes(PM_UART_PORT, tmp, sizeof(tmp), 20 / portTICK_PERIOD_MS) > 0)
        ;
}

void init()
{
    lastData = &storage;

    uart_config_t uart_config = {};
    uart_config.baud_rate = PM_UART_BAUDRATE;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;

    uart_param_config(PM_UART_PORT, &uart_config);
    uart_set_pin(PM_UART_PORT, PM_UART_TX_IO, PM_UART_RX_IO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(PM_UART_PORT, 256, 0, 0, NULL, 0);

    vTaskDelay(pdMS_TO_TICKS(50));
    uart_write_bytes(PM_UART_PORT, (const char *)passiveModeCmd, sizeof(passiveModeCmd));
    isMeasuring = true;
    vTaskDelay(pdMS_TO_TICKS(50));
    flushInput();
}

static bool readPMSdata()
{
    uint8_t buffer[32];
    uint16_t sum = 0;
    int len = uart_read_bytes(PM_UART_PORT, buffer, sizeof(buffer), 100 / portTICK_PERIOD_MS);
    if (len < 32 || buffer[0] != 0x42)
        return false;

    for (uint8_t i = 0; i < 30; i++)
        sum += buffer[i];

    uint16_t buffer_u16[15];
    for (uint8_t i = 0; i < 15; i++)
    {
        buffer_u16[i] = (buffer[2 + i * 2] << 8) + buffer[2 + i * 2 + 1];
    }

    memcpy((void *)lastData, (void *)buffer_u16, 30);

    if (sum != lastData->checksum)
    {
        ESP_LOGW(TAG, "Checksum failure");
        return false;
    }
    lastData->pm25_env = (lastData->pm25_env * 0.62f) + 0.5f;
    lastData->pm100_env = (lastData->pm100_env * 0.55f) + 0.3f;
    return true;
}

void startMeasure()
{
    if (isMeasuring)
        return;
    isMeasuring = true;
    uart_write_bytes(PM_UART_PORT, (const char *)pmsWakeCmd, sizeof(pmsWakeCmd));
    vTaskDelay(pdMS_TO_TICKS(50));
    uart_write_bytes(PM_UART_PORT, (const char *)passiveModeCmd, sizeof(passiveModeCmd));
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "PMS5003 measuring started");
}

pms5003data *endMeasure()
{
    if (!isMeasuring)
        return lastData;
    flushInput();

    uart_write_bytes(PM_UART_PORT, (const char *)requestReadCmd, sizeof(requestReadCmd));
    vTaskDelay(pdMS_TO_TICKS(50));

    for (int i = 0; i < 10; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(20));
        if (readPMSdata())
        {
            isMeasuring = false;
            uart_write_bytes(PM_UART_PORT, (const char *)pmsSleepCmd, sizeof(pmsSleepCmd));
            return lastData;
        }
    }

    return nullptr;
}
} // namespace PMS5003
#endif
