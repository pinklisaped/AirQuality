#pragma once

#include "settings.h"

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
void init(bool periodicMeasurement = true);
void startMeasure();
scd41data *endMeasure();
scd41data *measure();
void resetSCD41();
} // namespace SCD41