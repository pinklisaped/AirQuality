#pragma once
#include "esp_err.h"
#include <cstdint>

namespace ZB_HANDLER
{
enum class ZbState
{
    FactoryNew,      // Waiting pairing (Factory New)
    PairingProgress, // Paiting (Network Steering)
    Connected,       // Successful connection
    FirstConnected,  // Successful first connection
    Disconnected     // Error
};
using ZbStateCallback = void (*)(ZbState state);
void registerStateCallback(ZbStateCallback cb);

typedef struct
{
    uint16_t cluster_id;
    uint16_t attr_id;
    float value;
} zb_float_attr_update_t;

// Length-prefixed strings required by Zigbee standard
#define ESP_MANUFACTURER_NAME                                                                                          \
    "\x09"                                                                                                             \
    "ESPRESSIF"
#define ESP_MODEL_IDENTIFIER                                                                                           \
    "\x0a"                                                                                                             \
    "AirQuality"

#define ESP_ZIGBEE_PRIMARY_CHANNEL_MASK ((1U << 13))
#define ESP_ZIGBEE_SECONDARY_CHANNEL_MASK (0x07FFF800U)
#define ESP_ZIGBEE_STORAGE_PARTITION_NAME "zb_storage"

#define SENSOR_ENDPOINT_ID 1
#define CO2_ATTR_ID ESP_ZB_ZCL_ATTR_CARBON_DIOXIDE_MEASUREMENT_CONCENTRATION_MEASURED_VALUE_ID
#define PM25_ATTR_ID ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID
#define PM40_ATTR_ID 0x0000
#define PM100_ATTR_ID 0x0001
#define ALTITUDE_ATTR_ID ESP_ZB_ZCL_ATTR_ANALOG_OUTPUT_PRESENT_VALUE_ID

// Type for the altitude callback function
typedef void (*altitude_cb_t)(uint16_t);

esp_err_t init(void);
void start();
void reset();
void updateCO2(uint16_t value);
void updatePMs(float pm25, float pm100);
void updateTempHum(float temp, float hum);
void set_altitude_callback(altitude_cb_t cb);

} // namespace ZB_HANDLER