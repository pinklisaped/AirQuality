#include "esp_log.h"
#include "settings.h"
#include <algorithm>
#include <cstring>
#include <display.h>

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <math.h>
#include <stdio.h>
#include <string>

namespace DISPLAY
{

static const char *TAG = "GC9A01";

struct
{
    lv_obj_t *aqi_circle;
    lv_obj_t *aqi_val;

    lv_obj_t *pm25_arc;
    lv_obj_t *pm25_val;

    lv_obj_t *pm10_arc;
    lv_obj_t *pm10_val;

    lv_obj_t *co2_arc;
    lv_obj_t *co2_val;

    lv_obj_t *temp_val;
    lv_obj_t *humid_val;
} ui;

void initDashboard();
void createCornerWidget(int x, int y, int start_angle, int end_angle, const char *sub_text, lv_obj_t **out_arc,
                        lv_obj_t **out_val);
void createTextWidget(int x, int y, const char *sub_text, lv_obj_t **out_val);
void createCentralAQI();
void updateCO2(uint16_t co2, float temp, float humid);
void updatePM(uint16_t pm25, uint16_t pm10);

static void init_brightness()
{
    ledc_timer_config_t ledc_timer = {};
    ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_timer.duty_resolution = LEDC_TIMER_13_BIT;
    ledc_timer.timer_num = LEDC_TIMER_0;
    ledc_timer.freq_hz = 5000;
    ledc_timer.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {};
    ledc_channel.gpio_num = LCD_PIN_BK_LIGHT;
    ledc_channel.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_channel.channel = LEDC_CHANNEL_0;
    ledc_channel.timer_sel = LEDC_TIMER_0;
    ledc_channel.duty = 8191; // 100%
    ledc_channel_config(&ledc_channel);
}

void init()
{
    esp_lcd_panel_handle_t panel_handle = NULL;
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = LCD_PIN_MOSI;
    buscfg.miso_io_num = -1;
    buscfg.sclk_io_num = LCD_PIN_SCLK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = LCD_H_RES * LCD_DRAW_BUF_HEIGHT * sizeof(uint16_t);

    spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = LCD_PIN_CS;
    io_config.dc_gpio_num = LCD_PIN_DC;
    io_config.spi_mode = 0;
    io_config.pclk_hz = 40 * 1000 * 1000;
    io_config.trans_queue_depth = 10;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;

    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &io_handle);

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = LCD_PIN_RST;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    panel_config.bits_per_pixel = 16;

    esp_lcd_new_panel_gc9a01(io_handle, &panel_config, &panel_handle);

    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    // esp_lcd_panel_mirror(panel_handle, false, true);
    esp_lcd_panel_invert_color(panel_handle, true);
    esp_lcd_panel_disp_on_off(panel_handle, true);
    init_brightness();

    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&lvgl_cfg);

    lvgl_port_display_cfg_t disp_cfg = {};
    disp_cfg.io_handle = io_handle;
    disp_cfg.panel_handle = panel_handle;
    disp_cfg.buffer_size = LCD_H_RES * LCD_DRAW_BUF_HEIGHT;
    disp_cfg.double_buffer = true;
    disp_cfg.hres = LCD_H_RES;
    disp_cfg.vres = LCD_V_RES;
    disp_cfg.rotation = {
        .swap_xy = false,
        .mirror_x = false,
        .mirror_y = true,
    };
    disp_cfg.flags.buff_dma = true;
    disp_cfg.flags.swap_bytes = true;

    lvgl_port_add_disp(&disp_cfg);

    ESP_LOGI(TAG, "Display and LVGL initialized");

    fillScreen(COLOR_BLACK);
    initDashboard();
}

void initDashboard()
{
    lvgl_port_lock(0);

    // PM2.5
    createCornerWidget(-65, -45, 180, 255, "PM2.5", &ui.pm25_arc, &ui.pm25_val);
    // PM10
    createCornerWidget(65, -45, 285, 360, "PM10", &ui.pm10_arc, &ui.pm10_val);
    // CO2
    createCornerWidget(0, 80, 45, 135, "CO2", &ui.co2_arc, &ui.co2_val);

    createCentralAQI();
    createTextWidget(-75, 40, "Temp", &ui.temp_val);
    createTextWidget(75, 40, "Humid", &ui.humid_val);

    lvgl_port_unlock();
}

void createTextWidget(int x, int y, const char *sub_text, lv_obj_t **out_val)
{
    // Value container
    lv_obj_t *cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(cont, 80, 50);
    lv_obj_set_style_bg_opa(cont, 0, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_align(cont, LV_ALIGN_CENTER, x, y);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    // Value
    *out_val = lv_label_create(cont);
    lv_label_set_text(*out_val, "-");
    lv_obj_set_style_text_color(*out_val, lv_color_hex(COLOR_WHITE), 0);
    lv_obj_set_style_text_font(*out_val, &lv_font_montserrat_24, 0);
    lv_obj_align(*out_val, LV_ALIGN_CENTER, 0, -3);

    // Label
    lv_obj_t *sub = lv_label_create(cont);
    lv_label_set_text(sub, sub_text);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(COLOR_LIGHTGREY), 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 16);
}

void createCentralAQI()
{
    ui.aqi_circle = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui.aqi_circle, 80, 80);
    lv_obj_set_style_radius(ui.aqi_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(ui.aqi_circle, LV_ALIGN_CENTER, 0, -5);

    // Circle
    lv_obj_set_style_border_width(ui.aqi_circle, 0, 0);
    lv_obj_set_style_shadow_width(ui.aqi_circle, 0, 0);
    lv_obj_set_style_bg_color(ui.aqi_circle, lv_color_hex(COLOR_DARKGREY), 0);
    lv_obj_set_style_bg_opa(ui.aqi_circle, 255, 0);
    lv_obj_clear_flag(ui.aqi_circle, LV_OBJ_FLAG_SCROLLABLE);

    // Text
    ui.aqi_val = lv_label_create(ui.aqi_circle);
    lv_label_set_text(ui.aqi_val, "-");
    lv_obj_set_style_text_color(ui.aqi_val, lv_color_hex(COLOR_BLACK), 0);
    lv_obj_set_style_text_font(ui.aqi_val, &lv_font_montserrat_48, 0);
    lv_obj_center(ui.aqi_val);
}

void createCornerWidget(int x, int y, int start_angle, int end_angle, const char *sub_text, lv_obj_t **out_arc,
                        lv_obj_t **out_val)
{
    // Arc
    *out_arc = lv_arc_create(lv_scr_act());
    lv_obj_set_size(*out_arc, 240, 240);
    lv_obj_center(*out_arc);
    lv_arc_set_bg_angles(*out_arc, start_angle, end_angle);
    lv_arc_set_value(*out_arc, 100);

    lv_obj_remove_style(*out_arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(*out_arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(*out_arc, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_color(*out_arc, lv_color_hex(COLOR_DARKGREY), LV_PART_INDICATOR);

    // Value container
    lv_obj_t *cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(cont, 80, 60);
    lv_obj_set_style_bg_opa(cont, 0, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(cont, LV_ALIGN_CENTER, x, y);

    // Value
    *out_val = lv_label_create(cont);
    lv_label_set_text(*out_val, "-");
    lv_obj_set_style_text_color(*out_val, lv_color_hex(COLOR_WHITE), 0);
    lv_obj_set_style_text_font(*out_val, &lv_font_montserrat_28, 0);
    lv_obj_align(*out_val, LV_ALIGN_CENTER, 0, -3);

    // Label
    lv_obj_t *sub = lv_label_create(cont);
    lv_label_set_text(sub, sub_text);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(COLOR_LIGHTGREY), 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 16);
}

void updateCO2(uint16_t co2, float temp, float humid)
{
    char buf[32]; // Buffer for formatted strings
    lvgl_port_lock(0);

    // CO2
    lv_label_set_text_fmt(ui.co2_val, "%d", co2);
    lv_obj_set_style_arc_color(ui.co2_arc, lv_color_hex(getCO2Color(co2)), LV_PART_INDICATOR);

    // Temperature
    snprintf(buf, sizeof(buf), "%.1f°", temp);
    lv_label_set_text(ui.temp_val, buf);

    // Humidity
    snprintf(buf, sizeof(buf), "%.1f%%", humid);
    lv_label_set_text(ui.humid_val, buf);

    lvgl_port_unlock();
}

void updatePM(uint16_t pm25, uint16_t pm10)
{
    lvgl_port_lock(0);

    // PM2.5
    lv_label_set_text_fmt(ui.pm25_val, "%d", pm25);
    lv_obj_set_style_arc_color(ui.pm25_arc, lv_color_hex(getPM25Color(pm25)), LV_PART_INDICATOR);

    // PM10
    lv_label_set_text_fmt(ui.pm10_val, "%d", pm10);
    lv_obj_set_style_arc_color(ui.pm10_arc, lv_color_hex(getPM10Color(pm10)), LV_PART_INDICATOR);

    lvgl_port_unlock();
}

void updateAQI(uint16_t aqi)
{
    lvgl_port_lock(0);
    int text_color = aqi < 150 ? COLOR_BLACK : COLOR_WHITE;
    int color = getAQIColor(aqi);

    lv_label_set_text_fmt(ui.aqi_val, "%d", aqi);
    lv_obj_set_style_bg_color(ui.aqi_circle, lv_color_hex(color), 0);
    lv_obj_set_style_text_color(ui.aqi_val, lv_color_hex(text_color), 0);

    if (aqi < 100)
        lv_obj_set_style_text_font(ui.aqi_val, &lv_font_montserrat_48, 0);
    else
        lv_obj_set_style_text_font(ui.aqi_val, &lv_font_montserrat_30, 0);

    lvgl_port_unlock();
}

void setBrightness(uint16_t level)
{
    int duty = (8191 * level) / 255;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void fillScreen(int color)
{
    lvgl_port_lock(0);

    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN);

    lvgl_port_unlock();
}

int getAQIColor(uint16_t aqi)
{
    if (aqi <= 50)
        return COLOR_GREEN;
    if (aqi <= 100)
        return COLOR_YELLOW;
    if (aqi <= 150)
        return COLOR_ORANGE;
    if (aqi <= 200)
        return COLOR_RED;
    if (aqi <= 300)
        return COLOR_PURPLE;
    return COLOR_MAROON;
}

int getCO2Color(uint16_t co2)
{
    if (co2 <= 1000)
        return COLOR_GREEN;
    if (co2 <= 1500)
        return COLOR_YELLOW;
    if (co2 <= 1800)
        return COLOR_ORANGE;
    if (co2 <= 2000)
        return COLOR_RED;
    if (co2 <= 3000)
        return COLOR_PURPLE;
    return COLOR_MAROON;
}

int getPM25Color(uint16_t pm25)
{
    if (pm25 <= 12)
        return COLOR_GREEN;
    if (pm25 <= 35)
        return COLOR_YELLOW;
    if (pm25 <= 55)
        return COLOR_ORANGE;
    if (pm25 <= 150)
        return COLOR_RED;
    if (pm25 <= 250)
        return COLOR_PURPLE;
    return COLOR_MAROON;
}

int getPM10Color(uint16_t pm10)
{
    if (pm10 <= 54)
        return COLOR_GREEN;
    if (pm10 <= 154)
        return COLOR_YELLOW;
    if (pm10 <= 254)
        return COLOR_ORANGE;
    if (pm10 <= 354)
        return COLOR_RED;
    if (pm10 <= 424)
        return COLOR_PURPLE;
    return COLOR_MAROON;
}

} // namespace DISPLAY