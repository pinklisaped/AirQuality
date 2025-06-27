#include "pms5003.h"
#include "scd41.h"
#include "settings.h"
#include <Arduino.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <TFT_eSPI.h>
#include <math.h>

TFT_eSPI tft = TFT_eSPI();
SoftwareSerial pmsSerial(D3, D6);
uint16_t co2AQI = 0;
uint16_t pmsAQI = 0;

void drawDataArc(int xc, int yc, int r, int startAngle, int endAngle, uint16_t color, int thickness)
{
    for (int angle = startAngle; angle <= endAngle; angle++)
    {
        float rad = angle * DEG_TO_RAD;
        int x = xc + cos(rad) * r;
        int y = yc + sin(rad) * r;
        tft.fillCircle(x, y, thickness, color);
    }
}

uint16_t getAQIColor(int aqi)
{
    if (aqi <= 50)
        return TFT_GREEN;
    if (aqi <= 100)
        return TFT_YELLOW;
    if (aqi <= 150)
        return TFT_ORANGE;
    if (aqi <= 200)
        return TFT_RED;
    if (aqi <= 300)
        return TFT_PURPLE;
    return TFT_MAROON;
}
uint16_t getCO2Color(int co2)
{
    if (co2 <= 1000)
        return TFT_GREEN;
    if (co2 <= 1500)
        return TFT_YELLOW;
    if (co2 <= 1800)
        return TFT_ORANGE;
    if (co2 <= 2000)
        return TFT_RED;
    if (co2 <= 3000)
        return TFT_PURPLE;
    return TFT_MAROON;
}

uint16_t getPM25Color(int pm25)
{
    if (pm25 <= 12)
        return TFT_GREEN;
    if (pm25 <= 35)
        return TFT_YELLOW;
    if (pm25 <= 55)
        return TFT_ORANGE;
    if (pm25 <= 150)
        return TFT_RED;
    if (pm25 <= 250)
        return TFT_PURPLE;
    return TFT_MAROON;
}

uint16_t getPM10Color(int pm10)
{
    if (pm10 <= 54)
        return TFT_GREEN;
    if (pm10 <= 154)
        return TFT_YELLOW;
    if (pm10 <= 254)
        return TFT_ORANGE;
    if (pm10 <= 354)
        return TFT_RED;
    if (pm10 <= 424)
        return TFT_PURPLE;
    return TFT_MAROON;
}

uint16_t calculateAQIComponentPM(uint16_t concentration, bool isPM25)
{
    // EPA AQI
    struct
    {
        uint16_t c_low, c_high;
        uint16_t aqi_low, aqi_high;
    } breakpoints[] = {{0, 12, 0, 50},       {13, 35, 51, 100},    {36, 55, 101, 150},  {56, 150, 151, 200},
                       {151, 250, 201, 300}, {251, 350, 301, 400}, {351, 500, 401, 500}};

    float conc = (float)concentration;
    if (!isPM25)
        conc /= 1.5;

    for (auto &bp : breakpoints)
    {
        if (conc <= bp.c_high)
        {
            return (uint8_t)(((bp.aqi_high - bp.aqi_low) * (conc - bp.c_low)) / (bp.c_high - bp.c_low) + bp.aqi_low);
        }
    }
    return 500;
}

uint16_t calculateAQIComponentCO2(uint16_t concentration)
{
    // Define AQI ranges with corresponding CO2 concentration thresholds
    const struct
    {
        uint16_t conc_low;  // Lower CO2 concentration threshold (ppm)
        uint16_t conc_high; // Upper CO2 concentration threshold (ppm)
        uint16_t aqi_low;   // Lower AQI value
        uint16_t aqi_high;  // Upper AQI value
    } aqi_table[] = {
        {0, 400, 0, 0},         // Fresh air, outdoor baseline
        {401, 1000, 0, 30},     // Normal indoor level, acceptable
        {1001, 1500, 31, 50},   // Slight discomfort may begin
        {1501, 2000, 51, 80},   // Increased discomfort, fatigue
        {2001, 3000, 81, 100},  // Moderately unhealthy
        {3001, 4000, 101, 150}, // Unhealthy for sensitive groups
        {4001, 5000, 151, 200}, // Unhealthy, within SCD41 range
        {5001, 6000, 201, 300}, // Very unhealthy
        {6001, 10000, 301, 400} // Hazardous, beyond typical SCD41 range
    };

    // Iterate through the table to find the appropriate range
    for (int i = 0; i < sizeof(aqi_table) / sizeof(aqi_table[0]); i++)
    {
        if (concentration <= aqi_table[i].conc_high)
        {
            // Perform linear interpolation
            uint16_t conc_range = aqi_table[i].conc_high - aqi_table[i].conc_low;
            uint16_t aqi_range = aqi_table[i].aqi_high - aqi_table[i].aqi_low;
            uint16_t conc_diff = concentration - aqi_table[i].conc_low;
            return aqi_table[i].aqi_low + (conc_diff * aqi_range) / conc_range;
        }
    }
    // Return maximum AQI for concentrations exceeding the table
    return 400;
}

uint16_t calculateAQIPMs(const PMS5003::pms5003data &pmsData)
{
    uint16_t aqi_pm25 = calculateAQIComponentPM(pmsData.pm25_env, true);
    uint16_t aqi_pm10 = calculateAQIComponentPM(pmsData.pm100_env, false);
    return max(aqi_pm25, aqi_pm10);
}

void drawAQICircle(int x, int y, int aqi)
{
    uint16_t color = getAQIColor(aqi);
    tft.fillCircle(x, y, 35, color);
    if (aqi <= 100)
        tft.setTextColor(TFT_BLACK);
    if (aqi < 100)
    {
        tft.setTextSize(2);
        tft.drawCentreString(String(aqi), x, y - 20, 4);
    }
    else
        tft.drawCentreString(String(aqi), x, y - 10, 4);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    tft.drawCentreString("AQI", x, y + 35, 2);
}

void drawDashboardSCD(const SCD41::scd41data &scdData)
{
    tft.setTextColor(TFT_WHITE);
    // Clear values
    int w = tft.textWidth(String("000000"), 4);
    int h = tft.fontHeight(4);
    int offsetX = w / 2, offsetY = h / 2;
    // CO2
    tft.fillRect(120 - offsetX, 195 - offsetY, w, h, TFT_BLACK);
    drawDataArc(120, 120, 115, 45, 135, getCO2Color(scdData.co2), 3);
    tft.drawString(String(scdData.co2), 120, 195, 4);

    // Temp
    tft.fillRect(40 - offsetX, 150 - offsetY, w, h, TFT_BLACK);
    tft.drawString(String(scdData.temperature, 1), 40, 150, 4);

    // Humid
    tft.fillRect(200 - offsetX, 150 - offsetY, w, h, TFT_BLACK);
    tft.drawString(String(scdData.humidity, 1) + "%", 200, 150, 4);

    // AQI
    uint16_t aqi = max(pmsAQI, co2AQI);
    drawAQICircle(120, 120, aqi);
}

void drawDashboardPMS(const PMS5003::pms5003data &pmsData)
{
    tft.setTextColor(TFT_WHITE);
    // Clear values
    int w = tft.textWidth(String("000000"), 4);
    int h = tft.fontHeight(4);
    int offsetX = w / 2, offsetY = h / 2;

    // Values
    // PM2.5
    tft.fillRect(50 - offsetX, 75 - offsetY, w, h, TFT_BLACK);
    tft.drawString(String(pmsData.pm25_env), 50, 75, 4);
    drawDataArc(120, 120, 115, 180, 255, getPM25Color(pmsData.pm25_env), 3);

    // PM10
    tft.fillRect(190 - offsetX, 75 - offsetY, w, h, TFT_BLACK);
    tft.drawString(String(pmsData.pm100_env), 190, 75, 4);
    drawDataArc(120, 120, 115, 285, 360, getPM10Color(pmsData.pm100_env), 3);

    // AQI
    uint16_t aqi = max(pmsAQI, co2AQI);
    drawAQICircle(120, 120, aqi);
}

ulong pmsTime;
ulong co2Time;
void setup()
{
#ifdef DEBUG_ENABLE
    Serial.begin(9600);
#endif

    // Init display
    tft.init();
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE);
    tft.drawCentreString("Loading...", 120, 120, 4);

    // Init dust sensor
    SCD41::initSCD41();

    // Init CO2 sensor
    pmsSerial.begin(9600);
    delay(500);
    PMS5003::initPMS5003(&pmsSerial);
    PMS5003::startMeasure();

    pmsTime = millis() - 150000;
    co2Time = millis();
}

bool initializedCO2 = false;
bool initializedPMS = false;
void loop()
{
    if (millis() - co2Time > 5000)
    {
        if (!initializedCO2)
        {
            tft.fillScreen(TFT_BLACK);
            // Descriptions
            tft.setTextColor(TFT_LIGHTGREY);
            tft.drawString("CO", 120, 215, 2);
            tft.drawString("2", 120 + 12, 219, 1);
            tft.drawString("Temp", 40, 170, 2);
            tft.drawString("Humid", 200, 170, 2);
            initializedCO2 = true;
        }
        SCD41::scd41data *scdData = SCD41::measure();
        if (scdData == nullptr)
            return;
        Serial.printf("CO2: %d ppm, Temp: %.2f °C, Humidity: %.2f %%\n", scdData->co2, scdData->temperature,
                      scdData->humidity);
        co2AQI = calculateAQIComponentCO2(scdData->co2);
        drawDashboardSCD(*scdData);
        co2Time = millis();
    }

    if (millis() - pmsTime > 180000 && !PMS5003::isMeasuring)
        PMS5003::startMeasure();
    else if (millis() - pmsTime > 240000 && PMS5003::isMeasuring)
    {
        PMS5003::pms5003data *pmsData = PMS5003::endMeasure();
        if (pmsData != nullptr)
        {
            Serial.printf("AQI PM2.5: %d, PM10: %d ppm, CO2: %d ppm\n",
                          calculateAQIComponentPM(pmsData->pm25_env, true),
                          calculateAQIComponentPM(pmsData->pm100_env, false), co2AQI);

            // reading data was successful!
            DEBUG("---------------------------------------");
            DEBUG("Concentration Units (standard)");
            Serial.print("PM 1.0: ");
            Serial.print(pmsData->pm10_standard);
            Serial.print("\t\tPM 2.5: ");
            Serial.print(pmsData->pm25_standard);
            Serial.print("\t\tPM 10: ");
            DEBUG(pmsData->pm100_standard);
            DEBUG("---------------------------------------");
            DEBUG("Concentration Units (environmental)");
            Serial.print("PM 1.0: ");
            Serial.print(pmsData->pm10_env);
            Serial.print("\t\tPM 2.5: ");
            Serial.print(pmsData->pm25_env);
            Serial.print("\t\tPM 10: ");
            DEBUG(pmsData->pm100_env);
            DEBUG("---------------------------------------");
            Serial.print("Particles > 0.3um / 0.1L air:");
            DEBUG(pmsData->particles_03um);
            Serial.print("Particles > 0.5um / 0.1L air:");
            DEBUG(pmsData->particles_05um);
            Serial.print("Particles > 1.0um / 0.1L air:");
            DEBUG(pmsData->particles_10um);
            Serial.print("Particles > 2.5um / 0.1L air:");
            DEBUG(pmsData->particles_25um);
            Serial.print("Particles > 5.0um / 0.1L air:");
            DEBUG(pmsData->particles_50um);
            Serial.print("Particles > 10.0 um / 0.1L air:");
            DEBUG(pmsData->particles_100um);
            DEBUG("---------------------------------------");

            if (!initializedPMS)
            {
                // Descriptions
                tft.setTextColor(TFT_LIGHTGREY);
                tft.drawString("PM2.5", 50, 95, 2);
                tft.drawString("PM10", 190, 95, 2);
                initializedPMS = true;
            }
            pmsAQI = calculateAQIPMs(*pmsData);
            drawDashboardPMS(*pmsData);
            pmsTime = millis();
        }
    }
}
