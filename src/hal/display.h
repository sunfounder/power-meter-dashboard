#pragma once

#include "esp_lcd_panel_io.h"

#define LCD_H_RES 320
#define LCD_V_RES 170
#define LCD_PIXEL_CLOCK_HZ (6528000)
#define LCD_BUF_SIZE (LCD_H_RES * LCD_V_RES)

void display_init();

void display_draw_bitmap(void *user_data, int x1, int y1, int x2, int y2,
                         const void *color_map);

void display_set_on_flush_ready(void (*callback)(void *user_ctx));

esp_lcd_panel_handle_t display_get_panel_handle();

// 设置背光亮度 (0-255)
void display_set_backlight(uint8_t duty);

// 渐变开启背光
void display_backlight_fade_in(uint16_t duration_ms = 500);

// 显示启动画面（使用 LVGL）
void display_show_splash(const char *product_name, const char *version);
