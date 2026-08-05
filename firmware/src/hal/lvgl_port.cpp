#include "lvgl_port.h"
#include "display.h"
#include "lvgl.h"
#include <Arduino.h>

// 初始化 LVGL
static lv_display_t *disp;                    // 屏幕
static lv_color_t *lv_disp_buf;               // 显存
lv_group_t *menu_group;                       // 按键组
static bool is_initialized_lvgl = false;      // 标志位
static uint32_t last_refresh_time = millis(); // 上次刷新时间

/**
 * @brief 刷新回调函数
 * 用于通知 LVGL 刷新显示缓冲区
 * @param user_ctx 用户上下文指针（未使用）
 */
static void on_flush_ready(void *user_ctx) {
  // 检查 LVGL 是否初始化完成
  if (!is_initialized_lvgl || !disp) {
    return;
  }

  // 通知 LVGL 刷新显示缓冲区
  lv_display_flush_ready(disp);
}

/**
 * @brief 刷新回调函数
 * 用于将 LVGL 显示缓冲区刷新到屏幕
 * @param d 显示对象指针
 * @param area 刷新区域指针
 * @param px_map 像素映射指针
 */
static void lvgl_flush_cb(lv_display_t *d, const lv_area_t *area,
                          uint8_t *px_map) {
  // 从显示对象获取 LCD 面板句柄
  esp_lcd_panel_handle_t panel_handle =
      (esp_lcd_panel_handle_t)lv_display_get_user_data(d);
  // 调用显示驱动绘制位图
  display_draw_bitmap(panel_handle, area->x1, area->y1, area->x2, area->y2,
                      px_map);
}

/**
 * @brief 初始化 LVGL 配置
 * 初始化按键、LVGL 和通用 UI 样式
 */
void lvgl_port_init() {
  // 初始化显示
  display_init();
  display_set_on_flush_ready(on_flush_ready);

  // 初始化 LVGL
  lv_init();
  // 分配显存
  lv_disp_buf = (lv_color_t *)heap_caps_malloc(
      LCD_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  // 创建显示对象
  disp = lv_display_create(LCD_H_RES, LCD_V_RES);
  // 设置颜色格式
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
  // 设置刷新回调
  lv_display_set_flush_cb(disp, lvgl_flush_cb);
  // 设置绘制缓冲区
  lv_display_set_buffers(disp, lv_disp_buf, NULL,
                         LCD_BUF_SIZE * sizeof(lv_color_t),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  // 设置用户数据
  lv_display_set_user_data(disp, (void *)display_get_panel_handle());
  // 创建输入组
  if (menu_group == NULL) {
    menu_group = lv_group_create();
    lv_group_set_default(menu_group);
  }
  is_initialized_lvgl = true;
  Serial.println("LVGL initialized");
}
void lvgl_port_loop() {
  // 计算时间增量
  uint32_t now = millis();
  lv_tick_inc(now - last_refresh_time);
  last_refresh_time = now;

  lv_timer_handler();
}

void lvgl_port_menu_group_focus_next() {
  if (menu_group != NULL)
    lv_group_focus_next(menu_group);
}
void lvgl_port_menu_group_focus_prev() {
  if (menu_group != NULL)
    lv_group_focus_prev(menu_group);
}
lv_obj_t *lvgl_port_menu_group_get_focused() {
  if (menu_group != NULL)
    return lv_group_get_focused(menu_group);
  return NULL;
}
void lvgl_port_menu_group_add(lv_obj_t *obj) {
  if (menu_group != NULL)
    lv_group_add_obj(menu_group, obj);
}
void lvgl_port_menu_group_clear() {
  if (menu_group != NULL)
    lv_group_remove_all_objs(menu_group);
}