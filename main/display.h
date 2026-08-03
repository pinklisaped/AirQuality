#pragma once
#include <stdint.h>

namespace DISPLAY
{
// Colors
#define COLOR_BLACK 0x000000
#define COLOR_NAVY 0x000080
#define COLOR_DARKGREEN 0x006400
#define COLOR_DARKCYAN 0x008B8B
#define COLOR_MAROON 0x800000
#define COLOR_PURPLE 0x800080
#define COLOR_OLIVE 0x808000
#define COLOR_LIGHTGREY 0xD3D3D3
#define COLOR_DARKGREY 0x636363
#define COLOR_BLUE 0x0000FF
#define COLOR_GREEN 0x00FF00
#define COLOR_CYAN 0x00FFFF
#define COLOR_RED 0xFF0000
#define COLOR_MAGENTA 0xFF00FF
#define COLOR_YELLOW 0xFFFF00
#define COLOR_WHITE 0xFFFFFF
#define COLOR_ORANGE 0xFFA500
#define COLOR_GREENYELLOW 0xADFF2F
#define COLOR_PINK 0xFC366A

void init();
void setBrightness(uint16_t level); // 0 .. 255
void fillScreen(int color);
void updateCO2(uint16_t co2, float temp, float humid);
void updatePM(uint16_t pm25, uint16_t pm10);
void updateAQI(uint16_t aqi);
int getAQIColor(uint16_t aqi);
int getCO2Color(uint16_t co2);
int getPM25Color(uint16_t pm25);
int getPM10Color(uint16_t pm10);

} // namespace DISPLAY
