#pragma once

#include "driver/gpio.h"
#include <cstdint>

namespace LED_BLINKING
{

/**
 * @brief Initialize the LED hardware and underlying timer.
 * @param gpio_num Target GPIO pin (defaults to GPIO 8 for ESP32-H2 SuperMini).
 */
void init(gpio_num_t gpio_num = GPIO_NUM_8);

/**
 * @brief Set static RGB color and refresh LED output.
 */
void setColor(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Clear pixel and turn off the physical LED.
 */
void clear();

/**
 * @brief Configure blinking parameters without starting the timer automatically.
 * @param period_ms Toggle interval in milliseconds.
 */
void setBlinkParams(uint32_t period_ms, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Start or restart the blinking animation using current parameters.
 */
void startBlinking();

/**
 * @brief Stop active blinking animation without clearing current LED state.
 */
void stopBlinking();
/**
 * @brief Set static RGB color for a specified duration in milliseconds, then turn off.
 * @param duration_ms Time in milliseconds to keep the LED on.
 */
void setTemporaryColor(uint32_t duration_ms, uint8_t r, uint8_t g, uint8_t b);
/**
 * @brief Stop blinking animation and completely clear the LED output.
 */
void reset();

} // namespace LED_BLINKING