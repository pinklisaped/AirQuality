#include "display.h"
#include "led_blinking.h"
#include "light_sensor.h"
#include "pms5003.h"
#include "scd41.h"
#include "sps30.h"
#include "zigbee_handler.h"
#include <algorithm>
#include <cmath>
#include <driver/rmt_tx.h>
#include <driver/spi_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "main";
static uint16_t co2_AQI = 0;
static uint16_t pms_AQI = 0;

// Calculate AQI for PM2.5/PM10
uint16_t calculateAQIComponentPM(uint16_t concentration, bool is_pm25)
{
    struct
    {
        uint16_t c_low, c_high;
        uint16_t aqi_low, aqi_high;
    } breakpoints[] = {{0, 12, 0, 50},       {13, 35, 51, 100},    {36, 55, 101, 150},  {56, 150, 151, 200},
                       {151, 250, 201, 300}, {251, 350, 301, 400}, {351, 500, 401, 500}};

    float conc = (float)concentration;
    if (!is_pm25)
        conc /= 1.5;

    for (auto &bp : breakpoints)
    {
        if (conc <= bp.c_high)
        {
            return (uint16_t)(((bp.aqi_high - bp.aqi_low) * (conc - bp.c_low)) / (bp.c_high - bp.c_low) + bp.aqi_low);
        }
    }
    return 500;
}

// Calculate AQI for CO2
uint16_t calculateAQIComponentCO2(uint16_t concentration)
{
    const struct
    {
        uint16_t conc_low, conc_high;
        uint16_t aqi_low, aqi_high;
    } aqi_table[] = {{0, 400, 0, 0},         {401, 1000, 0, 30},     {1001, 1500, 31, 50},
                     {1501, 2000, 51, 80},   {2001, 3000, 81, 100},  {3001, 4000, 101, 150},
                     {4001, 5000, 151, 200}, {5001, 6000, 201, 300}, {6001, 10000, 301, 400}};

    for (const auto &entry : aqi_table)
    {
        if (concentration <= entry.conc_high)
        {
            uint16_t conc_range = entry.conc_high - entry.conc_low;
            uint16_t aqi_range = entry.aqi_high - entry.aqi_low;
            uint16_t conc_diff = concentration - entry.conc_low;
            return entry.aqi_low + (conc_diff * aqi_range) / conc_range;
        }
    }
    return 400;
}

// Calculate combined AQI
uint16_t calculateAQIPMs(uint16_t pm25, uint16_t pm100)
{
    uint16_t aqi_pm25 = calculateAQIComponentPM(pm25, true);
    uint16_t aqi_pm10 = calculateAQIComponentPM(pm100, false);
    return std::max(aqi_pm25, aqi_pm10);
}

#ifdef CONFIG_USE_PMS5003
void measurePMS(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(30000));
    while (1)
    {
        if (!PMS5003::isMeasuring)
        {
            PMS5003::startMeasure();
            vTaskDelay(pdMS_TO_TICKS(30000));
        }
        else
        {
            PMS5003::pms5003data *pmsData = PMS5003::endMeasure();
            if (pmsData == nullptr)
                continue;
            pms_AQI = calculateAQIPMs(pmsData->pm25_env, pmsData->pm100_env);
            DISPLAY::updatePM(pmsData->pm25_env, pmsData->pm100_env);
            DISPLAY::updateAQI(std::max(pms_AQI, co2_AQI));
            ZB_HANDLER::updatePMs(pmsData->pm25_env, pmsData->pm100_env);

            // Reading data was successful!
            ESP_LOGI(TAG, "---------------------------------------");
            ESP_LOGI(TAG, "Concentration Units (standard)");
            ESP_LOGI(TAG, "PM 1.0: %u     PM 2.5: %u     PM 10: %u", pmsData->pm10_standard, pmsData->pm25_standard,
                     pmsData->pm100_standard);
            ESP_LOGI(TAG, "---------------------------------------");
            ESP_LOGI(TAG, "Concentration Units (environmental)");
            ESP_LOGI(TAG, "PM 1.0: %u     PM 2.5: %u     PM 10: %u", pmsData->pm10_env, pmsData->pm25_env,
                     pmsData->pm100_env);
            ESP_LOGI(TAG, "---------------------------------------");
            ESP_LOGI(TAG, "Particles > 0.3um / 0.1L air: %u", pmsData->particles_03um);
            ESP_LOGI(TAG, "Particles > 0.5um / 0.1L air: %u", pmsData->particles_05um);
            ESP_LOGI(TAG, "Particles > 1.0um / 0.1L air: %u", pmsData->particles_10um);
            ESP_LOGI(TAG, "Particles > 2.5um / 0.1L air: %u", pmsData->particles_25um);
            ESP_LOGI(TAG, "Particles > 5.0um / 0.1L air: %u", pmsData->particles_50um);
            ESP_LOGI(TAG, "Particles > 10.0 um / 0.1L air: %u", pmsData->particles_100um);
            ESP_LOGI(TAG, "---------------------------------------");
            ESP_LOGI("MEM", "Task %s unused stack: %d bytes", pcTaskGetName(NULL),
                     (int)uxTaskGetStackHighWaterMark(NULL));

            vTaskDelay(pdMS_TO_TICKS(150000));
        }
    }
}
#elifdef CONFIG_USE_SPS30
void measureSPS(void *pvParameters)
{
    while (1)
    {
        if (!SPS30::isMeasuring)
        {
            SPS30::startMeasure();
        }
        else
        {
            SPS30::sps30data *data = SPS30::endMeasure();
            if (data == nullptr)
                continue;

            uint16_t pm25 = roundf(data->mass_pm2_5);
            uint16_t pm100 = roundf(data->mass_pm10_0);
            pms_AQI = calculateAQIPMs(pm25, pm100);
            DISPLAY::updatePM(pm25, pm100);
            DISPLAY::updateAQI(std::max(pms_AQI, co2_AQI));
            ZB_HANDLER::updatePMs(data->mass_pm2_5, data->mass_pm10_0);

            // Reading data was successful!
            ESP_LOGI(TAG, "---------------------------------------");
            ESP_LOGI(TAG, "Concentration Units (environmental)");
            ESP_LOGI(TAG, "PM 1.0: %.2f     PM 2.5: %.2f     PM 10: %.2f", data->mass_pm1_0, data->mass_pm2_5,
                     data->mass_pm10_0);
            ESP_LOGI(TAG, "---------------------------------------");
            ESP_LOGI(TAG, "PM1.0 Mass Concentration: %.2f ug/m3", data->mass_pm1_0);
            ESP_LOGI(TAG, "PM2.5 Mass Concentration: %.2f ug/m3", data->mass_pm2_5);
            ESP_LOGI(TAG, "PM4.0 Mass Concentration: %.2f ug/m3", data->mass_pm4_0);
            ESP_LOGI(TAG, "PM10.0 Mass Concentration: %.2f ug/m3", data->mass_pm10_0);
            // Note: SPS30 provides particles per cm3, while PMS5003 provides per 0.1L
            ESP_LOGI(TAG, "Particles > 0.5um / cm3: %.2f", data->num_pm0_5);
            ESP_LOGI(TAG, "Particles > 1.0um / cm3: %.2f", data->num_pm1_0);
            ESP_LOGI(TAG, "Particles > 2.5um / cm3: %.2f", data->num_pm2_5);
            ESP_LOGI(TAG, "Particles > 4.0um / cm3: %.2f", data->num_pm4_0);
            ESP_LOGI(TAG, "Particles > 10.0um / cm3: %.2f", data->num_pm10_0);
            ESP_LOGI(TAG, "Typical Particle Size: %.3f um", data->typical_size);
            ESP_LOGI(TAG, "---------------------------------------");

            ESP_LOGI("MEM", "Task %s unused stack: %d bytes", pcTaskGetName(NULL),
                     (int)uxTaskGetStackHighWaterMark(NULL));
            vTaskDelay(pdMS_TO_TICKS(150000));
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
#endif

void measureSCD(void *pvParameters)
{
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(5000));
        SCD41::scd41data *scdData = SCD41::measure();
        if (scdData == nullptr)
            continue;
        co2_AQI = calculateAQIComponentCO2(scdData->co2);
        DISPLAY::updateCO2(scdData->co2, scdData->temperature, scdData->humidity);
        DISPLAY::updateAQI(std::max(pms_AQI, co2_AQI));
        ZB_HANDLER::updateCO2(scdData->co2);
        ZB_HANDLER::updateTempHum(scdData->temperature, scdData->humidity);

        // Reading data was successful!
        ESP_LOGI(TAG, "CO2: %d ppm, Temp: %.2f °C, Humidity: %.2f%%", scdData->co2, scdData->temperature,
                 scdData->humidity);
        // ESP_LOGI("MEM", "Task %s unused stack: %d bytes", pcTaskGetName(NULL),
        // (int)uxTaskGetStackHighWaterMark(NULL));
    }
}

void zigbeeButtonHandle(void *pvParameters)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ZIGBEE_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
        .hys_ctrl_mode = GPIO_HYS_CTRL_EFUSE,
    };
    gpio_config(&io_conf);

    while (1)
    {
        if (gpio_get_level(ZIGBEE_BUTTON_PIN) == 1)
        {
            uint32_t start_time = xTaskGetTickCount();
            bool activated = false;

            while (gpio_get_level(ZIGBEE_BUTTON_PIN) == 1)
            {
                uint32_t duration = (xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS;

                if (duration >= 5000 && !activated)
                {
                    ESP_LOGW(TAG, "Factory Reset Triggered...");
                    ZB_HANDLER::reset();
                    activated = true;
                }
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void checkBrightness(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(5000));
    int last_stable_duty = 255;
    const int adc_min = 300;
    const int adc_max = 3000;
    const uint8_t step = 5;
    const uint8_t min_duty = 1;
    const uint8_t max_duty = 255;

    while (1)
    {
        int lightLevel = LIGHT_SENSOR::readLightLevel();

        // Calculate scale 0.0 - 1.0
        float scale = (float)(lightLevel - adc_min) / (adc_max - adc_min);
        if (scale < 0.0f)
            scale = 0.0f;
        if (scale > 1.0f)
            scale = 1.0f;

        int target_duty = (int)(scale * (max_duty - min_duty)) + min_duty;

        target_duty = (target_duty / step) * step;

        if (target_duty < min_duty)
            target_duty = min_duty;
        if (target_duty > max_duty)
            target_duty = max_duty;

        if (abs(target_duty - last_stable_duty) >= step)
        {
            while (last_stable_duty != target_duty)
            {
                if (last_stable_duty < target_duty)
                {
                    last_stable_duty += step;
                    if (last_stable_duty > target_duty)
                        last_stable_duty = target_duty;
                }
                else
                {
                    last_stable_duty -= step;
                    if (last_stable_duty < target_duty)
                        last_stable_duty = target_duty;
                }

                int final_to_set = std::max((int)min_duty, std::min((int)max_duty, last_stable_duty));
                DISPLAY::setBrightness(final_to_set);

                vTaskDelay(pdMS_TO_TICKS(30));
            }
            ESP_LOGI("LIGHT", "Brightness stabilized at: %d", last_stable_duty);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void onZigbeeStateChanged(ZB_HANDLER::ZbState state)
{
    switch (state)
    {
    case ZB_HANDLER::ZbState::FactoryNew:
        LED_BLINKING::stopBlinking();
        LED_BLINKING::setBlinkParams(1000, 32, 16, 0); // Slow yellow
        LED_BLINKING::startBlinking();
        break;

    case ZB_HANDLER::ZbState::PairingProgress:
        LED_BLINKING::stopBlinking();
        LED_BLINKING::setBlinkParams(200, 0, 0, 32); // Fast blue
        LED_BLINKING::startBlinking();
        break;

    case ZB_HANDLER::ZbState::Connected:
        LED_BLINKING::setTemporaryColor(500, 0, 32, 0);
        break;

    case ZB_HANDLER::ZbState::FirstConnected:
        LED_BLINKING::setTemporaryColor(3000, 0, 32, 0);
        break;

    case ZB_HANDLER::ZbState::Disconnected:
        // Red
        LED_BLINKING::stopBlinking();
        LED_BLINKING::setColor(32, 0, 0);
        break;
    }
}

extern "C" void app_main(void)
{
    esp_log_level_set("*", LOG_LEVEL);

    LED_BLINKING::init(LED_PIN);
    // ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 0, 90, 128));
    // ESP_ERROR_CHECK(led_strip_refresh(led_strip));

    // vTaskDelay(pdMS_TO_TICKS(5000));

    // Initialize display
    DISPLAY::init();
    DISPLAY::setBrightness(5);

    // Initialize zigbee
    ZB_HANDLER::init();
    ZB_HANDLER::registerStateCallback(onZigbeeStateChanged);
    ZB_HANDLER::start();
    xTaskCreate(zigbeeButtonHandle, "zigbeeButtonHandle", 2048, NULL, 10, NULL);

    // Initialize sensors
    SCD41::init(true);

#ifdef CONFIG_USE_PMS5003
    PMS5003::init();
    xTaskCreate(measurePMS, "measurePMS", 2048, NULL, 5, NULL);
#elifdef CONFIG_USE_SPS30
    SPS30::init();
    xTaskCreate(measureSPS, "measureSPS", 4096, NULL, 5, NULL);
#endif
    xTaskCreate(measureSCD, "measureSCD", 2048, NULL, 5, NULL);

    // Initialize brightness
    LIGHT_SENSOR::init();
    xTaskCreate(checkBrightness, "checkBrightness", 2048, NULL, 10, NULL);

    ESP_LOGE(TAG, "ESP Started");
    while (1)
    {
        // Blink LED
        // ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 62, 0, 148)); // Blue
        // ESP_ERROR_CHECK(led_strip_refresh(led_strip));
        // vTaskDelay(pdMS_TO_TICKS(100));

        // // Shutdown LED
        // led_strip_clear(led_strip);

        vTaskDelay(pdMS_TO_TICKS(10000));

        ESP_LOGI("SYS", "Free heap: %" PRIu32 " bytes", esp_get_free_heap_size());
    }
}
