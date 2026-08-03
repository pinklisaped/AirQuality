#include "led_blinking.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "led_strip.h"

namespace LED_BLINKING
{

static led_strip_handle_t s_led_strip = nullptr;
static esp_timer_handle_t s_blink_timer = nullptr;
static esp_timer_handle_t s_off_timer = nullptr;

static struct
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint32_t period_ms = 1000;
    bool is_on = false;
} s_anim_state;

static void off_timer_cb(void *arg)
{
    clear();
}

static void blink_timer_cb(void *arg)
{
    if (!s_led_strip)
    {
        return;
    }

    if (s_anim_state.is_on)
    {
        led_strip_clear(s_led_strip);
        s_anim_state.is_on = false;
    }
    else
    {
        led_strip_set_pixel(s_led_strip, 0, s_anim_state.r, s_anim_state.g, s_anim_state.b);
        led_strip_refresh(s_led_strip);
        s_anim_state.is_on = true;
    }
}

void init(gpio_num_t gpio_num)
{
    led_strip_config_t strip_config = {.strip_gpio_num = gpio_num,
                                       .max_leds = 1,
                                       .led_model = LED_MODEL_WS2812,
                                       .color_component_format =
                                           LED_STRIP_COLOR_COMPONENT_FMT_GRB, // The color component format is G-R-B
                                       .flags = {.invert_out = false}};

    led_strip_rmt_config_t rmt_config = {.clk_src = RMT_CLK_SRC_DEFAULT,
                                         .resolution_hz = 10 * 1000 * 1000,
                                         .mem_block_symbols = 64,
                                         .flags = {
                                             .with_dma = false,
                                         }};

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip));
    led_strip_clear(s_led_strip);

    const esp_timer_create_args_t timer_args = {.callback = &blink_timer_cb,
                                                .arg = nullptr,
                                                .dispatch_method = ESP_TIMER_TASK,
                                                .name = "led_blink_timer",
                                                .skip_unhandled_events = false};
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_blink_timer));

    const esp_timer_create_args_t off_timer_args = {.callback = &off_timer_cb,
                                                    .arg = nullptr,
                                                    .dispatch_method = ESP_TIMER_TASK,
                                                    .name = "led_off_timer",
                                                    .skip_unhandled_events = false};
    ESP_ERROR_CHECK(esp_timer_create(&off_timer_args, &s_off_timer));
}

void setColor(uint8_t r, uint8_t g, uint8_t b)
{
    s_anim_state.r = r;
    s_anim_state.g = g;
    s_anim_state.b = b;
    if (s_led_strip)
    {
        led_strip_set_pixel(s_led_strip, 0, r, g, b);
        led_strip_refresh(s_led_strip);
        s_anim_state.is_on = true;
    }
}

void clear()
{
    if (s_led_strip)
    {
        led_strip_clear(s_led_strip);
        s_anim_state.is_on = false;
    }
}

void setBlinkParams(uint32_t period_ms, uint8_t r, uint8_t g, uint8_t b)
{
    s_anim_state.period_ms = period_ms;
    s_anim_state.r = r;
    s_anim_state.g = g;
    s_anim_state.b = b;
}

void startBlinking()
{
    if (s_blink_timer)
    {
        if (esp_timer_is_active(s_blink_timer))
        {
            esp_timer_stop(s_blink_timer);
        }
        esp_timer_start_periodic(s_blink_timer, s_anim_state.period_ms * 1000);
    }
}

void stopBlinking()
{
    if (s_blink_timer && esp_timer_is_active(s_blink_timer))
    {
        esp_timer_stop(s_blink_timer);
    }
    if (s_off_timer && esp_timer_is_active(s_off_timer))
    {
        esp_timer_stop(s_off_timer);
    }
}

void setTemporaryColor(uint32_t duration_ms, uint8_t r, uint8_t g, uint8_t b)
{
    // Останавливаем текущие анимации/таймеры
    stopBlinking();

    // Зажигаем нужный цвет
    setColor(r, g, b);

    // Запускаем однократный таймер на N миллисекунд (переводим ms в мкс)
    if (s_off_timer)
    {
        esp_timer_start_once(s_off_timer, duration_ms * 1000);
    }
}

void reset()
{
    stopBlinking();
    clear();
}

} // namespace LED_BLINKING