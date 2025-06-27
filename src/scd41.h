#pragma once

#include "settings.h"
#include <Wire.h>

namespace SCD41
{
struct scd41data
{
    uint16_t co2;
    float temperature;
    float humidity;
};

extern scd41data *lastData;
extern bool isMeasuring;
void initSCD41(TwoWire &wire = Wire, bool periodicMeasurement = true);
inline void initSCD41(bool periodicMeasurement)
{
    initSCD41(Wire, periodicMeasurement);
}

void startMeasure();
scd41data *endMeasure();
scd41data *measure();
void resetSCD41();
} // namespace SCD41
