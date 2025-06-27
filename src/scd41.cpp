#include "scd41.h"
#include <SensirionI2cScd4x.h>

namespace SCD41
{

bool isMeasuring = false;
static SensirionI2cScd4x scd4x;
static TwoWire *scdWirePtr = nullptr;
static scd41data storage = {};
scd41data *lastData = nullptr;

void initSCD41(TwoWire &wire, bool periodicMeasurement)
{
    lastData = &storage;
    scdWirePtr = &wire;
    scdWirePtr->begin();
    scd4x.begin(wire, 0x62);

    if (periodicMeasurement)
        scd4x.startPeriodicMeasurement();
    else
        scd4x.stopPeriodicMeasurement();
}

void startMeasure()
{
    if (isMeasuring)
        return;

    uint16_t error = scd4x.measureSingleShot();
    isMeasuring = (error == 0);
    if (error != 0)
        DEBUG("Measuring start end with error " + String(error));
}

scd41data *endMeasure()
{
    if (!isMeasuring)
        return lastData;

    bool isDataReady = false;
    uint16_t error = scd4x.getDataReadyStatus(isDataReady);
    if (error != 0 || !isDataReady)
        return nullptr;

    if (isDataReady)
        isMeasuring = false;

    uint16_t co2 = 0;
    float temperature = 0.0f;
    float humidity = 0.0f;

    error = scd4x.readMeasurement(co2, temperature, humidity);
    if (error == 0)
    {
        lastData->co2 = co2;
        lastData->temperature = temperature;
        lastData->humidity = humidity;
        return lastData;
    }

    return nullptr;
}

scd41data *measure()
{
    bool isDataReady = false;
    uint16_t error = scd4x.getDataReadyStatus(isDataReady);
    if (error != 0 || !isDataReady)
        return nullptr;

    uint16_t co2 = 0;
    float temperature = 0.0f;
    float humidity = 0.0f;

    error = scd4x.readMeasurement(co2, temperature, humidity);
    if (error == 0)
    {
        lastData->co2 = co2;
        lastData->temperature = temperature;
        lastData->humidity = humidity;
        return lastData;
    }

    return nullptr;
}
void resetSCD41()
{
    uint16_t error = scd4x.performFactoryReset();
    if (error)
    {
        DEBUG("Factory reset failed with error: " + String(error));
    }
    else
    {
        DEBUG("SCD41 factory reset successful.");
    }
}
} // namespace SCD41
