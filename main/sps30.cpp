#include "sps30.h"

#ifdef CONFIG_USE_SPS30
#include "settings.h"
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

namespace SPS30
{
#define SPS30_ADDR 0x69
static const char *TAG = "SPS30";

static i2c_master_bus_handle_t bus_handle = nullptr;
static i2c_master_dev_handle_t dev_handle = nullptr;

bool isMeasuring = false;
static sps30data storage = {};
sps30data *lastData = &storage;

// --- Command Definitions ---
static const uint8_t startMeasureCmd[] = {0x00, 0x10, 0x03, 0x00, 0xAC};
static const uint8_t stopMeasureCmd[] = {0x01, 0x04};
static const uint8_t readDataCmd[] = {0x03, 0x00};
static const uint8_t dataReadyCmd[] = {0x02, 0x02};
static const uint8_t fanCleaningCmd[] = {0x56, 0x07};
static const uint8_t softResetCmd[] = {0xD3, 0x04};

static esp_err_t send_cmd_array(const uint8_t *cmd, size_t len)
{
    if (dev_handle == nullptr)
        return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit(dev_handle, cmd, len, pdMS_TO_TICKS(200));
}

void init()
{
    if (bus_handle != nullptr)
        return; // Already initialized

    i2c_master_bus_config_t bus_conf = {};
    bus_conf.i2c_port = PM_I2C_PORT;
    bus_conf.sda_io_num = (gpio_num_t)PM_I2C_SDA_IO;
    bus_conf.scl_io_num = (gpio_num_t)PM_I2C_SCL_IO;
    bus_conf.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_conf.trans_queue_depth = 0;
    bus_conf.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&bus_conf, &bus_handle);
    if (err != ESP_OK)
        return;

    i2c_device_config_t dev_conf = {};
    dev_conf.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_conf.device_address = SPS30_ADDR;
    dev_conf.scl_speed_hz = 100000;

    err = i2c_master_bus_add_device(bus_handle, &dev_conf, &dev_handle);
    if (err != ESP_OK)
        return;

    // Wait for the sensor to stabilize after power-up
    vTaskDelay(pdMS_TO_TICKS(500));

    // Probe the device: check if it responds to its address
    err = i2c_master_probe(bus_handle, SPS30_ADDR, 500);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "SPS30 not found on I2C bus! Check wiring and 5V power.");
        return;
    }

    // Attempt soft reset
    send_cmd_array(softResetCmd, sizeof(softResetCmd));
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "SPS30 detected and initialized successfully");
}

void startMeasure()
{
    if (isMeasuring || dev_handle == nullptr)
        return;

    if (send_cmd_array(startMeasureCmd, sizeof(startMeasureCmd)) == ESP_OK)
    {
        isMeasuring = true;
        ESP_LOGI(TAG, "SPS30 measurement started");
    }
}

static float parse_float(uint8_t *data)
{
    // SPS30 format: 2 bytes data, 1 byte CRC, 2 bytes data, 1 byte CRC
    // Reconstruct 32-bit float skipping CRC bytes at indices 2 and 5
    uint32_t val = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[3] << 8) | (uint32_t)data[4];
    float f;
    memcpy(&f, &val, 4);
    return f;
}

static uint8_t i2c_rx_buf[60];
sps30data *endMeasure()
{
    // Basic safety checks
    if (!isMeasuring || dev_handle == nullptr)
    {
        return lastData;
    }

    // Data Ready Check
    uint8_t ready_res[3] = {0};
    esp_err_t err = i2c_master_transmit(dev_handle, dataReadyCmd, 2, pdMS_TO_TICKS(100));

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C Transmit error (Ready Check)");
        isMeasuring = false; // Reset state anyway
        return nullptr;
    }

    vTaskDelay(pdMS_TO_TICKS(10));

    err = i2c_master_receive(dev_handle, ready_res, 3, pdMS_TO_TICKS(100));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C Receive error (Ready Check)");
        isMeasuring = false;
        return nullptr;
    }

    if (ready_res[1] != 0x01)
    {
        ESP_LOGW(TAG, "Data not ready (Status 0x%02X)", ready_res[1]);
        return nullptr;
    }

    // Reading actual measurements
    err = i2c_master_transmit(dev_handle, readDataCmd, 2, pdMS_TO_TICKS(100));
    if (err != ESP_OK)
    {
        isMeasuring = false;
        return nullptr;
    }

    vTaskDelay(pdMS_TO_TICKS(10));

    err = i2c_master_receive(dev_handle, i2c_rx_buf, 60, pdMS_TO_TICKS(500));
    if (err == ESP_OK)
    {
        float *ptr = (float *)&storage;
        for (int i = 0; i < 10; i++)
        {
            ptr[i] = parse_float(&i2c_rx_buf[i * 6]);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Data read failed: %s", esp_err_to_name(err));
    }

    // Finalizing: Stop fan and reset flag
    send_cmd_array(stopMeasureCmd, sizeof(stopMeasureCmd));
    isMeasuring = false;

    return (err == ESP_OK) ? lastData : nullptr;
}

void startFanCleaning()
{
    if (dev_handle == nullptr)
        return;
    ESP_LOGI(TAG, "Initiating fan cleaning...");
    send_cmd_array(fanCleaningCmd, sizeof(fanCleaningCmd));
}

} // namespace SPS30
#endif