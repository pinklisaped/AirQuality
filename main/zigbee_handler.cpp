#include "zigbee_handler.h"
#include "alarm_timer.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_zigbee.h"
#include "ezbee/zha.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <cstdint>

namespace ZB_HANDLER
{

static const char *TAG = "ZB_HANDLER";
static bool s_initialized = false;

#define REPORTING_MASK (EZB_ZCL_ATTR_ACCESS_READ_WRITE | EZB_ZCL_ATTR_ACCESS_REPORTING)

static uint8_t zclVersion = 0x03;
static uint8_t powerSource = EZB_ZCL_BASIC_POWER_SOURCE_DC_SOURCE;
// EZB_ZCL_VALUE_NaN
static float pm25_val = 0x0000U;
static float pm25_min = 0.0;
static float pm25_max = 1000.0;

static float pm100_val = 0.0;
static float pm100_min = 0.0;
static float pm100_max = 1000.0;

static float co2_val = EZB_ZCL_CARBON_DIOXIDE_MEASUREMENT_MEASURED_VALUE_DEFAULT_VALUE; // default
static float co2_min = 0.000000f;                                                       // 0 ppm min
static float co2_max = 0.005000f;                                                       // 5000 ppm max

static int16_t temp_val = EZB_ZCL_TEMPERATURE_MEASUREMENT_MEASURED_VALUE_DEFAULT_VALUE;
static int16_t temp_min = -5000;
static int16_t temp_max = 10000;

static uint16_t hum_val = EZB_ZCL_REL_HUMIDITY_MEASUREMENT_MEASURED_VALUE_DEFAULT_VALUE;
static uint16_t hum_min = 0;
static uint16_t hum_max = 10000;

static float altitude = 0;
static float altitude_min_val = 0.0;
static float altitude_max_val = 4000.0;
static const char f_altitude_description[] = "\x08Altitude";
static altitude_cb_t s_altitude_cb = nullptr;
static void configure_reporting(uint16_t cluster_id, uint16_t attr_id, uint8_t attr_type, float delta_val);

static ZbStateCallback s_state_cb = nullptr;

void registerStateCallback(ZbStateCallback cb)
{
    s_state_cb = cb;
}
static void notifyState(ZbState state)
{
    if (s_state_cb != nullptr)
    {
        s_state_cb(state);
    }
}
/**
 * @brief Performs factory reset and restarts the ESP
 */
void reset()
{
    ESP_LOGW(TAG, "Factory reset triggered...");
    esp_zigbee_factory_reset();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

/**
 * @brief Register a callback for incoming altitude updates
 */
void set_altitude_callback(altitude_cb_t cb)
{
    s_altitude_cb = cb;
}

static void esp_zigbee_alarm_bdb_commissioning(alarm_timer_arg_t arg)
{
    esp_zigbee_lock_acquire(portMAX_DELAY);
    (void)ezb_bdb_start_top_level_commissioning(arg);
    esp_zigbee_lock_release();
}

/**
 * @brief Zigbee stack signals handler (Network steering, leave, etc.)
 */
static bool esp_zigbee_app_signal_handler(const ezb_app_signal_t *app_signal)
{
    ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
    ezb_app_signal_type_t signal_type = ezb_app_signal_get_type(app_signal);
    ESP_LOGW("ZB_LOG", ">>> Signal: 0x%x, Status: 0x%x", signal_type, status);

    switch (signal_type)
    {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP: {
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        notifyState(ZbState::PairingProgress);
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
    }
    break;
    case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
        if (status == EZB_BDB_STATUS_SUCCESS)
        {
            ESP_LOGI(TAG, "Device started up in%s factory-reset mode", ezb_bdb_is_factory_new() ? "" : " non");
            if (ezb_bdb_is_factory_new())
            {
                notifyState(ZbState::FactoryNew);
                ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
            }
            else
            {
                notifyState(ZbState::Connected);
                ESP_LOGI(TAG, "Device reboot & network restored successfully");
                s_initialized = true;
            }
        }
        else
        {
            ESP_LOGW(TAG, "%s failed with status(0x%02x), please retry", ezb_app_signal_to_string(signal_type), status);
            ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
        }
    }
    break;
    case EZB_BDB_SIGNAL_STEERING: {
        if (status == EZB_BDB_STATUS_SUCCESS)
        {
            ezb_extpanid_t extended_pan_id;
            ezb_nwk_get_extended_panid(&extended_pan_id);
            ESP_LOGI(
                TAG, "Joined network successfully: PAN ID(0x%04hx, EXT: 0x%llx), Channel(%d), Short Address(0x%04hx)",
                ezb_nwk_get_panid(), extended_pan_id.u64, ezb_nwk_get_current_channel(), ezb_nwk_get_short_address());
            notifyState(ZbState::FirstConnected);
            s_initialized = true;
        }
        else
        {
            notifyState(ZbState::Disconnected);
            ESP_LOGW(TAG, "Failed to join network with status(0x%02x)", status);
            alarm_timer_schedule(esp_zigbee_alarm_bdb_commissioning, EZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
    }
    break;
    case EZB_ZDO_SIGNAL_DEVICE_ANNCE: {
        const ezb_zdo_signal_device_annce_params_t *dev_annce_params =
            static_cast<const ezb_zdo_signal_device_annce_params_t *>(ezb_app_signal_get_params(app_signal));
        ESP_LOGI(TAG, "New device commissioned or rejoined (short: 0x%04hx)", dev_annce_params->short_addr);
    }
    break;
    case EZB_ZDO_SIGNAL_LEAVE: {
        const ezb_zdo_signal_leave_params_t *leave_params =
            static_cast<const ezb_zdo_signal_leave_params_t *>(ezb_app_signal_get_params(app_signal));
        ESP_LOGI(TAG, "Left network successfully with type(0x%02x)", leave_params->leave_type);
        reset();
    }
    break;
    default:
        ESP_LOGI(TAG, "Zigbee APP Signal: %s(type: 0x%02x)", ezb_app_signal_to_string(signal_type), signal_type);
        break;
    }
    return true;
}

static void esp_zigbee_zcl_core_action_handler(ezb_zcl_core_action_callback_id_t callback_id, void *message)
{
    switch (callback_id)
    {
    case EZB_ZCL_CORE_DEFAULT_RSP_CB_ID: {
        ezb_zcl_cmd_default_rsp_message_t *default_rsp = (ezb_zcl_cmd_default_rsp_message_t *)message;
        ESP_LOGI(TAG, "Received ZCL Default Response: status(0x%02x)", default_rsp->in.status_code);
    }
    break;
    default:
        ESP_LOGW(TAG, "ZCL Core Action: ID(0x%04lx)", callback_id);
        break;
    }
}

esp_err_t init(void)
{
    esp_err_t nv_status = nvs_flash_init();
    if (nv_status == ESP_ERR_NVS_NO_FREE_PAGES || nv_status == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGI(TAG, "ZB Flash init status: %d", nv_status);
        ESP_ERROR_CHECK(nvs_flash_erase());
        nv_status = nvs_flash_init();
    }
    ESP_LOGI(TAG, "ZB Flash init status: %d", nv_status);
    ESP_ERROR_CHECK(nv_status);
    ESP_ERROR_CHECK(nvs_flash_init_partition(ESP_ZIGBEE_STORAGE_PARTITION_NAME));

    /* 1. Stack config: Matching Role and Parameters for Stability */
    esp_zigbee_config_t zigbee_config = {};
    zigbee_config.device_config = {};
    zigbee_config.platform_config = {};
    zigbee_config.device_config.device_type = EZB_NWK_DEVICE_TYPE_ROUTER;
    zigbee_config.device_config.install_code_policy = false;
    zigbee_config.platform_config.storage_partition_name = ESP_ZIGBEE_STORAGE_PARTITION_NAME;
    zigbee_config.platform_config.radio_config.radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE;

#ifdef CONFIG_ZB_ZCZR
    zigbee_config.device_config.zczr_config = {
        .max_children = 10,
    };
#else
    zigbee_config.device_config.zed_config = {
        .ed_timeout = EZB_NWK_ED_TIMEOUT_64MIN,
        .keep_alive = 4000,
    };
#endif
    ESP_ERROR_CHECK(esp_zigbee_init(&zigbee_config));

    ezb_af_device_desc_t dev_desc = ezb_af_create_device_desc();
    // ezb_zha_common_device_config_t device_cfg = EZB_ZHA_COMMON_DEVICE_CONFIG();

    ezb_af_ep_config_t ep_config = {
        .ep_id = SENSOR_ENDPOINT_ID,
        .app_profile_id = EZB_AF_HA_PROFILE_ID,
        .app_device_id = EZB_ZHA_CONSUMPTION_AWARENESS_DEVICE_ID,
        .app_device_version = 0,
        .reserved = 4,
    };
    ezb_af_ep_desc_t ep_desc = ezb_af_create_endpoint_desc(&ep_config);
    ezb_zcl_cluster_desc_t identify_desc = NULL;
    ezb_zcl_cluster_desc_t basic_desc = NULL;
    ezb_zcl_cluster_desc_t temperature_desc = NULL;
    ezb_zcl_cluster_desc_t humidity_desc = NULL;
    ezb_zcl_cluster_desc_t co2_desc = NULL;
    ezb_zcl_cluster_desc_t pm25_desc = NULL;
    ezb_zcl_cluster_desc_t pm100_desc = NULL;

    ezb_zcl_basic_cluster_server_config_t basic_cfg = {
        .zcl_version = EZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = EZB_ZCL_BASIC_POWER_SOURCE_DC_SOURCE,
    };
    basic_desc = ezb_zcl_basic_create_cluster_desc(&basic_cfg, EZB_ZCL_CLUSTER_SERVER);
    ezb_zcl_basic_cluster_desc_add_attr(basic_desc, EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
                                        (void *)ESP_MANUFACTURER_NAME);
    ezb_zcl_basic_cluster_desc_add_attr(basic_desc, EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
                                        (void *)ESP_MODEL_IDENTIFIER);

    ezb_zcl_identify_cluster_server_config_t identify_cfg = {
        .identify_time = EZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE,
    };
    identify_desc = ezb_zcl_identify_create_cluster_desc(&identify_cfg, EZB_ZCL_CLUSTER_SERVER);

    ezb_zcl_temperature_measurement_cluster_server_config_t temperature_sensor_cfg = {
        .measured_value = temp_val,
        .min_measured_value = temp_min,
        .max_measured_value = temp_max,
    };
    temperature_desc =
        ezb_zcl_temperature_measurement_create_cluster_desc(&temperature_sensor_cfg, EZB_ZCL_CLUSTER_SERVER);

    ezb_zcl_rel_humidity_measurement_cluster_server_config_t humidity_cfg = {
        .measured_value = hum_val,
        .min_measured_value = hum_min,
        .max_measured_value = hum_max,
    };
    humidity_desc = ezb_zcl_rel_humidity_measurement_create_cluster_desc(&humidity_cfg, EZB_ZCL_CLUSTER_SERVER);

    ezb_zcl_carbon_dioxide_measurement_cluster_server_config_t co2_cfg = {
        .measured_value = co2_val,
        .min_measured_value = co2_min,
        .max_measured_value = co2_max,
    };
    co2_desc = ezb_zcl_carbon_dioxide_measurement_create_cluster_desc(&co2_cfg, EZB_ZCL_CLUSTER_SERVER);

    ezb_zcl_pm2_5_measurement_cluster_server_config_t pm25_cfg = {
        .measured_value = pm25_val,
        .min_measured_value = pm25_min,
        .max_measured_value = pm25_max,
    };
    pm25_desc = ezb_zcl_pm2_5_measurement_create_cluster_desc(&pm25_cfg, EZB_ZCL_CLUSTER_SERVER);

    ezb_zcl_custom_cluster_config_t pm100_cfg = {
        .cluster_id = 0xff00,
        .init_func = NULL,
        .deinit_func = NULL,
    };
    pm100_desc = ezb_zcl_custom_create_cluster_desc(&pm100_cfg, EZB_ZCL_CLUSTER_SERVER);
    ezb_zcl_custom_cluster_desc_add_attr(pm100_desc, PM100_ATTR_ID, EZB_ZCL_ATTR_TYPE_SINGLE,
                                         EZB_ZCL_ATTR_ACCESS_READ | EZB_ZCL_ATTR_ACCESS_REPORTING, (void *)&pm100_val);

    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(ep_desc, basic_desc));
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(ep_desc, identify_desc));
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(ep_desc, temperature_desc));
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(ep_desc, humidity_desc));
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(ep_desc, co2_desc));
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(ep_desc, pm25_desc));
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(ep_desc, pm100_desc));

    ESP_ERROR_CHECK(ezb_af_device_add_endpoint_desc(dev_desc, ep_desc));
    ESP_ERROR_CHECK(ezb_af_device_desc_register(dev_desc));

    ezb_zcl_core_action_handler_register(esp_zigbee_zcl_core_action_handler);

    return ESP_OK;
} // namespace ZB_HANDLER

static void zigbee_main_loop(void *pvParameters)
{
    esp_zigbee_launch_mainloop();
    ESP_LOGE(TAG, "Zigbee stack loop exited!");
    esp_zigbee_deinit();
    vTaskDelete(NULL);
}

esp_err_t esp_zigbee_setup_commissioning(void)
{
    ezb_aps_secur_enable_distributed_security(false);
    ESP_ERROR_CHECK(ezb_bdb_set_primary_channel_set(ESP_ZIGBEE_PRIMARY_CHANNEL_MASK));
    ESP_ERROR_CHECK(ezb_bdb_set_secondary_channel_set(ESP_ZIGBEE_SECONDARY_CHANNEL_MASK));
    ESP_ERROR_CHECK(ezb_app_signal_add_handler(esp_zigbee_app_signal_handler));

    return ESP_OK;
}

void start()
{
    ESP_LOGI(TAG, "Starting Zigbee stack task...");
    ESP_ERROR_CHECK(esp_zigbee_setup_commissioning());
    esp_err_t err = esp_zigbee_start(false);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "ezb_start failed: 0x%x", err);
        return;
    }
    xTaskCreate(zigbee_main_loop, "Zigbee_main", 8192, NULL, 15, NULL);
}

void updateCO2(uint16_t value)
{
    co2_val = (float)value / 1000000.0f;

    if (!s_initialized)
        return;

    ESP_LOGI(TAG, "Updated CO2 %.6f", co2_val);

    // Lock the stack for direct write operations
    esp_zigbee_lock_acquire(portMAX_DELAY);

    ezb_zcl_status_t zcl_status = ezb_zcl_set_attr_value(
        SENSOR_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_CARBON_DIOXIDE_MEASUREMENT, EZB_ZCL_CLUSTER_SERVER,
        EZB_ZCL_ATTR_CARBON_DIOXIDE_MEASUREMENT_MEASURED_VALUE_ID, EZB_ZCL_STD_MANUF_CODE, &co2_val, false);

    if (zcl_status != 0)
        ESP_LOGE(TAG, "Set CO2 value. zcl_status: %d", zcl_status);

    esp_zigbee_lock_release();
}

void updatePMs(float pm25, float pm100)
{
    pm25_val = pm25;
    pm100_val = pm100;
    if (!s_initialized)
        return;

    esp_zigbee_lock_acquire(portMAX_DELAY);

    // PM2.5
    ezb_zcl_set_attr_value(SENSOR_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_PM2_5_MEASUREMENT, EZB_ZCL_CLUSTER_SERVER,
                           EZB_ZCL_ATTR_PM2_5_MEASUREMENT_MEASURED_VALUE_ID, EZB_ZCL_STD_MANUF_CODE, &pm25_val, false);

    // PM10
    // ezb_zcl_set_attr_value(SENSOR_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_ANALOG_INPUT, EZB_ZCL_CLUSTER_SERVER,
    //                        EZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID, EZB_ZCL_STD_MANUF_CODE, &pm100_val, false);
    ezb_zcl_set_attr_value(SENSOR_ENDPOINT_ID, 0xff00, EZB_ZCL_CLUSTER_SERVER, PM100_ATTR_ID, EZB_ZCL_STD_MANUF_CODE,
                           &pm100_val, false);

    esp_zigbee_lock_release();
}

void updateTempHum(float temp, float hum)
{
    // Convert values to Zigbee standard formats (multiplied by 100)
    temp_val = (int16_t)(temp * 100);
    hum_val = (uint16_t)(hum * 100);

    if (!s_initialized)
        return;

    esp_zigbee_lock_acquire(portMAX_DELAY);

    // Temperature
    ezb_zcl_status_t zcl_status = ezb_zcl_set_attr_value(
        SENSOR_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_TEMPERATURE_MEASUREMENT, EZB_ZCL_CLUSTER_SERVER,
        EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_MEASURED_VALUE_ID, EZB_ZCL_STD_MANUF_CODE, &temp_val, false);

    if (zcl_status != 0)
        ESP_LOGE(TAG, "Set Temperature value. zcl_status: %d", zcl_status);

    zcl_status = ezb_zcl_set_attr_value(SENSOR_ENDPOINT_ID, EZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT,
                                        EZB_ZCL_CLUSTER_SERVER, EZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_MEASURED_VALUE_ID,
                                        EZB_ZCL_STD_MANUF_CODE, &hum_val, false);
    if (zcl_status != 0)
        ESP_LOGE(TAG, "Set Humidity value. zcl_status: %d", zcl_status);

    esp_zigbee_lock_release();
}
} // namespace ZB_HANDLER
