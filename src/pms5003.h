#pragma once
#include "settings.h"
#include <Arduino.h>

namespace PMS5003
{
struct pms5003data
{
    uint16_t framelen;
    uint16_t pm10_standard, pm25_standard, pm100_standard;
    uint16_t pm10_env, pm25_env, pm100_env;
    uint16_t particles_03um, particles_05um, particles_10um, particles_25um, particles_50um, particles_100um;
    uint16_t unused;
    uint16_t checksum;
};

extern bool isMeasuring;
extern pms5003data *lastData;

void initPMS5003(Stream *s);
void startMeasure();
pms5003data *endMeasure();
} // namespace PMS5003
