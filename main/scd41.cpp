#include "scd41.h"
#include "settings.h"
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

namespace SCD41
{
static const char *TAG = "SCD4x";
static const uint8_t SCD41_I2C_ADDR = 0x62;
static i2c_master_bus_handle_t bus_handle = nullptr;
static i2c_master_dev_handle_t dev_handle = nullptr; // CHANGED: Persistent device handle

scd41data *lastData = nullptr;
bool isMeasuring = false;

#define CMD_START_PERIODIC 0x21b1
#define CMD_READ_MEASUREMENT 0xec05
#define CMD_STOP_PERIODIC 0x3f86
#define CMD_FACTORY_RESET 0x3632
#define CMD_WAKE_UP 0x36f6

static void i2c_bus_soft_reset()
{
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << SCD_I2C_SDA_IO) | (1ULL << SCD_I2C_SCL_IO);
    io_conf.mode = GPIO_MODE_OUTPUT_OD;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    for (int i = 0; i < 9; i++)
    {
        gpio_set_level(SCD_I2C_SCL_IO, 0);
        esp_rom_delay_us(10);
        gpio_set_level(SCD_I2C_SCL_IO, 1);
        esp_rom_delay_us(10);
    }
    gpio_set_level(SCD_I2C_SDA_IO, 1);
    esp_rom_delay_us(10);
}

static esp_err_t i2c_master_init()
{
    if (bus_handle != nullptr)
    {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = (i2c_port_num_t)SCD_I2C_PORT;
    bus_cfg.sda_io_num = (gpio_num_t)SCD_I2C_SDA_IO;
    bus_cfg.scl_io_num = (gpio_num_t)SCD_I2C_SCL_IO;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.trans_queue_depth = 0; // FIXED: Disable buggy async engine (switch to synchronous)
    bus_cfg.flags.enable_internal_pullup = 1;

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

static esp_err_t scd41_write_cmd(uint16_t cmd)
{
    if (dev_handle == nullptr)
        return ESP_ERR_INVALID_STATE;

    uint8_t cmd_buf[2] = {(uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF)};

    // FIXED: Directly transmit using persistent handle without dynamic add/rm
    esp_err_t err = i2c_master_transmit(dev_handle, cmd_buf, 2, pdMS_TO_TICKS(200));
    if (err != ESP_OK)
    {
        ESP_LOGD(TAG, "Failed to send command 0x%04x: %s", cmd, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t scd41_read(uint8_t *data, size_t len)
{
    if (dev_handle == nullptr)
        return ESP_ERR_INVALID_STATE;

    // FIXED: Directly receive using persistent handle
    esp_err_t err = i2c_master_receive(dev_handle, data, len, pdMS_TO_TICKS(500));
    if (err != ESP_OK)
    {
        ESP_LOGD(TAG, "Failed to read data: %s", esp_err_to_name(err));
    }
    return err;
}

void init(bool periodicMeasurement)
{
    if (lastData != nullptr)
    {
        delete lastData;
        lastData = nullptr;
    }

    i2c_bus_soft_reset();

    if (i2c_master_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C initialization failed");
        return;
    }

    // FIXED: Add device to the bus ONCE during initialization
    if (dev_handle == nullptr)
    {
        i2c_device_config_t dev_cfg = {};
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address = SCD41_I2C_ADDR;
        dev_cfg.scl_speed_hz = SCD_I2C_FREQ_HZ;

        esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to add SCD41 device to bus: %s", esp_err_to_name(err));
            return;
        }
    }

    // Stop any ongoing measurements
    if (scd41_write_cmd(CMD_STOP_PERIODIC) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to stop periodic measurement");
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Wake up sensor
    if (scd41_write_cmd(CMD_WAKE_UP) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to wake up SCD41");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    if (periodicMeasurement)
    {
        if (scd41_write_cmd(CMD_START_PERIODIC) != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to start periodic measurement");
            isMeasuring = false;
        }
        else
        {
            ESP_LOGI(TAG, "Periodic measurement started");
            isMeasuring = true;
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    lastData = new scd41data();
}

void startMeasure()
{
    if (!isMeasuring)
    {
        if (dev_handle == nullptr)
        {
            ESP_LOGE(TAG, "Device handle not initialized");
            return;
        }
        if (scd41_write_cmd(CMD_START_PERIODIC) != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to start periodic measurement");
            isMeasuring = false;
        }
        else
        {
            ESP_LOGI(TAG, "Periodic measurement started");
            isMeasuring = true;
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

scd41data *measure()
{
    if (!isMeasuring || dev_handle == nullptr)
    {
        ESP_LOGE(TAG, "Measurement not started or handle null");
        return nullptr;
    }

    if (scd41_write_cmd(CMD_READ_MEASUREMENT) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send read command");
        return nullptr;
    }
    vTaskDelay(pdMS_TO_TICKS(50)); // Essential delay for SCD41 execution time

    uint8_t data[9];
    if (scd41_read(data, 9) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read measurement");
        return nullptr;
    }

    uint16_t co2 = (data[0] << 8) | data[1];
    uint16_t temp_raw = (data[3] << 8) | data[4];
    uint16_t hum_raw = (data[6] << 8) | data[7];

    lastData->co2 = co2;
    lastData->temperature = -45.0f + 175.0f * (float)temp_raw / 65536.0f;
    lastData->humidity = 100.0f * (float)hum_raw / 65536.0f;

    return lastData;
}

scd41data *endMeasure()
{
    if (!isMeasuring || dev_handle == nullptr)
    {
        ESP_LOGE(TAG, "Measurement not started");
        return nullptr;
    }

    if (scd41_write_cmd(CMD_STOP_PERIODIC) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to stop periodic measurement");
    }
    isMeasuring = false;
    vTaskDelay(pdMS_TO_TICKS(1000));

    return lastData;
}

void resetSCD41()
{
    if (dev_handle == nullptr) return;

    if (scd41_write_cmd(CMD_STOP_PERIODIC) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to stop periodic measurement");
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (scd41_write_cmd(CMD_FACTORY_RESET) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to perform factory reset");
    }
    isMeasuring = false;
    vTaskDelay(pdMS_TO_TICKS(1000));
}
} // namespace SCD41