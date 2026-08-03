#pragma once

#include <sdkconfig.h>

#ifdef CONFIG_USE_SPS30

#include <stdint.h>

namespace SPS30
{
struct sps30data
{
    float mass_pm1_0;
    float mass_pm2_5;
    float mass_pm4_0;
    float mass_pm10_0;
    float num_pm0_5;
    float num_pm1_0;
    float num_pm2_5;
    float num_pm4_0;
    float num_pm10_0;
    float typical_size;
};

extern bool isMeasuring;
extern sps30data *lastData;

void init();
void startMeasure();
sps30data *endMeasure();
void startFanCleaning();
} // namespace SPS30
#endif
