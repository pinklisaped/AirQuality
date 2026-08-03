#include "light_sensor.h"
#include "settings.h"
#include <driver/gpio.h>
#include <esp_adc/adc_continuous.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_log.h>

namespace LIGHT_SENSOR
{

static const char *TAG = "LIGHT_SENSOR";
adc_oneshot_unit_handle_t adc1_handle;
adc_channel_t channel;

void init()
{
    // Get ADC channel by PIN
    adc_unit_t unit;
    esp_err_t err = adc_continuous_io_to_channel(LIGHT_LEVEL_PIN, &unit, &channel);

    if (err == ESP_OK)
        ESP_LOGI(TAG, "GPIO 4 mapping: Unit %d, Channel %d", unit, channel);

    adc_oneshot_unit_init_cfg_t init_config1 = {};
    init_config1.unit_id = unit;
    init_config1.ulp_mode = ADC_ULP_MODE_DISABLE;

    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    // Config ADC channel
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, channel, &config));

    ESP_LOGI(TAG, "ADC initialized on GPIO 4");
}

int readLightLevel()
{
    int raw_val = 0;
    esp_err_t ret = adc_oneshot_read(adc1_handle, channel, &raw_val);

    if (ret == ESP_OK)
    {
        // ESP_LOGI(TAG, "Raw: %d", raw_val);
        return raw_val;
    }
    else
    {
        ESP_LOGE(TAG, "ADC read failed");
        return -1;
    }
}
} // namespace LIGHT_SENSOR