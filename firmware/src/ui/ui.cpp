#include "ui.h"
#include "../hal/display.h"
#include "../hal/lvgl_port.h"

#include <Arduino.h>

#define VKEY_SIZE 30
#define VKEY_RADIUS (VKEY_SIZE / 2)
#define VKEY_GAP 6
#define VKEY_COL_WIDTH (VKEY_SIZE + VKEY_GAP)
#define APP_CONTAINER_WIDTH (LCD_H_RES - VKEY_COL_WIDTH)
#define ROW_WIDTH APP_CONTAINER_WIDTH
#define ROW_HEIGHT 34
#define ROW_HORIZONTAL_PADDING 12

uint16_t long_press_duration = 1000;

static ui_obj_t *app_container; // APP 容器

// 虚拟按键对象
static ui_obj_t *vkey_up;
static ui_obj_t *vkey_down;
static ui_obj_t *vkey_enter;
static ui_obj_t *vkey_back;

// 高亮虚拟按键
static void highlight_vkey(ui_obj_t *vkey) {
  if (!vkey) return;
  lv_obj_set_style_bg_color(vkey, UI_MAIN_COLOR, 0);
  lv_obj_set_style_bg_opa(vkey, LV_OPA_COVER, 0);
}

// 恢复虚拟按键颜色
static void restore_vkey(ui_obj_t *vkey) {
  if (!vkey) return;
  lv_obj_set_style_bg_color(vkey, UI_COLOR_DARK_GRAY, 0);
  lv_obj_set_style_bg_opa(vkey, LV_OPA_COVER, 0);
}

// 创建圆形虚拟按键
static ui_obj_t* create_vkey(ui_obj_t *parent, const char *icon) {
  ui_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, VKEY_SIZE, VKEY_SIZE);
  lv_obj_set_style_radius(btn, VKEY_RADIUS, 0);
  lv_obj_set_style_bg_color(btn, UI_COLOR_DARK_GRAY, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  
  // 创建图标标签
  ui_obj_t *label = lv_label_create(btn);
  lv_label_set_text(label, icon);
  lv_obj_set_style_text_color(label, UI_COLOR_WHITE, 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
  
  return btn;
}

void ui_draw_virtual_keys(ui_obj_t *parent) {
  // 创建分割线（在按键区域右侧）
  lv_obj_t *divider = lv_obj_create(parent);
  lv_obj_set_size(divider, 1, LCD_V_RES);
  lv_obj_set_pos(divider, VKEY_COL_WIDTH - 1, 0);
  lv_obj_set_style_bg_color(divider, lv_color_hex(0x333333), 0); // 浅灰色分割线
  lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(divider, 0, 0);
  lv_obj_set_style_radius(divider, 0, 0);
  lv_obj_set_style_pad_all(divider, 0, 0);
  lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
  
  // 创建 4 个圆形虚拟按键，垂直排列在左侧
  // 从上到下：UP, BACK, ENTER, DOWN
  vkey_up = create_vkey(parent, LV_SYMBOL_UP);        // Up - 最上
  vkey_back = create_vkey(parent, LV_SYMBOL_LEFT);    // Back - 第 2
  vkey_enter = create_vkey(parent, LV_SYMBOL_OK);     // Enter - 第 3
  vkey_down = create_vkey(parent, LV_SYMBOL_DOWN);    // Down - 最下
  
  // 4 个按键均匀排列，上下贴边
  // 总可用高度 = LCD_V_RES
  // 每个按键间隔 = (LCD_V_RES - VKEY_SIZE) / 3
  int spacing = (LCD_V_RES - VKEY_SIZE) / 3;
  
  // 左侧列（贴左边），从上到下均匀分布
  lv_obj_set_pos(vkey_up, 0, 0);
  lv_obj_set_pos(vkey_back, 0, spacing);
  lv_obj_set_pos(vkey_enter, 0, spacing * 2);
  lv_obj_set_pos(vkey_down, 0, LCD_V_RES - VKEY_SIZE);
}

// 根据虚拟按键类型高亮
void ui_highlight_vkey_by_type(int vkey_type) {
  // 先恢复所有按键
  ui_restore_all_vkeys();
  
  // 再高亮目标按键
  switch (vkey_type) {
    case 1: highlight_vkey(vkey_up); break;
    case 2: highlight_vkey(vkey_down); break;
    case 3: highlight_vkey(vkey_enter); break;
    case 4: highlight_vkey(vkey_back); break;
  }
}

// 恢复所有虚拟按键颜色
void ui_restore_all_vkeys() {
  restore_vkey(vkey_up);
  restore_vkey(vkey_down);
  restore_vkey(vkey_enter);
  restore_vkey(vkey_back);
}

void ui_draw_main_container() {
  ui_obj_t *current_screen = lv_screen_active();
  lv_obj_set_style_bg_color(current_screen, UI_COLOR_BLACK, 0);

  // 创建虚拟按键
  ui_draw_virtual_keys(current_screen);

  // 创建中间内容的主容器（在两侧虚拟按键之间）
  app_container = lv_obj_create(current_screen);
  lv_obj_set_size(app_container, APP_CONTAINER_WIDTH, LCD_V_RES);
  lv_obj_set_pos(app_container, VKEY_COL_WIDTH, 0);
  lv_obj_set_style_bg_opa(app_container, 0, 0);
  lv_obj_set_style_border_width(app_container, 0, 0);
  lv_obj_set_flex_flow(app_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(app_container, 0, 0);
  lv_obj_set_style_radius(app_container, 0, 0);
  lv_obj_set_style_pad_gap(app_container, 0, 0);

  // 设置全局字体 (use default — custom font removed to save Flash)
  // LV_FONT_DECLARE(lv_font_wen_yuan_rounded_sc_14);
  // lv_obj_set_style_text_font(app_container, &lv_font_wen_yuan_rounded_sc_14, 0);
}

void ui_init(uint16_t _long_press_duration) {
  long_press_duration = _long_press_duration;
}

ui_obj_t *ui_create_page() {
  // 清空组，防止按键焦点留在已经消失的页面上
  lv_obj_clean(app_container);
  lvgl_port_menu_group_clear();

  // 创建新容器
  ui_obj_t *app_root = lv_obj_create(app_container);
  lv_obj_set_size(app_root, ROW_WIDTH, LCD_V_RES);
  lv_obj_set_style_bg_opa(app_root, 0, 0);
  lv_obj_set_style_border_width(app_root, 0, 0);
  lv_obj_set_flex_flow(app_root, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(app_root, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_set_style_pad_all(app_root, 0, 0);
  lv_obj_set_style_radius(app_root, 0, 0);
  lv_obj_set_style_pad_gap(app_root, 0, 0);

  return app_root;
}

ui_obj_t *ui_create_row(ui_obj_t *parent, ui_event_cb_t onclick) {
  // 创建行容器
  ui_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, ROW_WIDTH, ROW_HEIGHT);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);    // 不可滚动
  lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS); // 滚动时聚焦
  
  // 分隔线
  ui_obj_t *div = lv_obj_create(row);
  lv_obj_set_size(div, ROW_WIDTH - ROW_HORIZONTAL_PADDING * 2, 1);
  lv_obj_align(div, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(div, UI_COLOR_GRAY, 0);

  // 默认样式
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_layout(row, 0, 0);
  lv_obj_set_style_radius(row, 0, 0);
  lv_obj_set_style_bg_color(row, UI_COLOR_BLACK, 0);
  lv_obj_set_style_border_width(row, 3, 0);
  lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
  lv_obj_set_style_border_color(row, UI_COLOR_BLACK, 0);
  lv_obj_set_style_bg_color(div, UI_COLOR_GRAY, 0);
  lv_obj_set_style_border_width(div, 0, 0);

  // 选中样式
  lv_obj_set_style_border_color(row, UI_COLOR_MAIN, LV_STATE_FOCUSED);
  lv_obj_set_style_bg_color(row, UI_COLOR_DARK_GRAY, LV_STATE_FOCUSED);
  lv_obj_set_style_bg_opa(div, LV_OPA_COVER, LV_STATE_FOCUSED);

  lvgl_port_menu_group_add(row);
  
  // 如果传入了回调函数，则设置用户数据
  if (onclick != NULL) {
    lv_obj_set_user_data(row, (void *)onclick);
  }
  return row;
}

ui_row_label_t ui_create_label(ui_obj_t *parent, const char *title,
                               const char *trailing, ui_event_cb_t onclick) {
  ui_row_label_t item;
  item.root = ui_create_row(parent, onclick);
  
  // 标题标签
  item.label = lv_label_create(item.root);
  lv_obj_add_flag(item.label, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_label_set_text(item.label, title);
  lv_obj_align(item.label, LV_ALIGN_LEFT_MID, ROW_HORIZONTAL_PADDING, 0);
  lv_obj_set_style_text_color(item.label, UI_COLOR_WHITE, 0);
  lv_obj_set_style_text_color(item.label, UI_COLOR_MAIN, LV_STATE_FOCUSED);

  // trailing 标签
  item.trailing = lv_label_create(item.root);
  lv_obj_add_flag(item.trailing, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_label_set_text(item.trailing, trailing);
  lv_obj_align(item.trailing, LV_ALIGN_RIGHT_MID, -ROW_HORIZONTAL_PADDING, 0);
  lv_obj_set_style_text_color(item.trailing, UI_COLOR_LIGHT_GRAY, 0);
  lv_obj_set_style_text_color(item.trailing, UI_COLOR_MAIN, LV_STATE_FOCUSED);

  return item;
}

ui_row_progress_t ui_create_progress_bar(ui_obj_t *parent, const char *title,
                                         const char *trailing, int min, int max,
                                         ui_event_cb_t onclick) {
  ui_row_progress_t item;
  ui_row_label_t label = ui_create_label(parent, title, trailing, onclick);
  item.root = label.root;
  item.label = label.label;
  item.trailing = label.trailing;

  // 进度条
  item.bar = lv_bar_create(item.root);
  lv_obj_set_size(item.bar, ROW_WIDTH, 2);
  lv_obj_align(item.bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_bar_set_range(item.bar, min, max);
  lv_bar_set_value(item.bar, min, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(item.bar, UI_COLOR_BLACK, 0);
  lv_obj_set_style_bg_color(item.bar, UI_MAIN_COLOR, LV_PART_INDICATOR);
  lv_obj_set_style_radius(item.bar, 0, 0);
  lv_obj_set_style_radius(item.bar, 0, LV_PART_INDICATOR);

  return item;
}

ui_row_3_col_t ui_create_3_col_item(ui_obj_t *parent, const char *title,
                                    const char *value1, const char *value2,
                                    ui_event_cb_t onclick) {
  ui_row_3_col_t item;
  item.root = ui_create_row(parent, onclick);
  item.label = lv_label_create(item.root);
  lv_label_set_text(item.label, title);
  lv_obj_set_style_text_color(item.label, UI_COLOR_WHITE, 0);
  lv_obj_set_style_text_color(item.label, lv_color_white(), LV_STATE_FOCUSED);
  lv_obj_align(item.label, LV_ALIGN_LEFT_MID, ROW_HORIZONTAL_PADDING, 0);

  item.value1 = lv_label_create(item.root);
  lv_label_set_text(item.value1, value1);
  lv_obj_set_style_text_color(item.value1, UI_COLOR_WHITE, 0);
  lv_obj_set_style_text_color(item.value1, UI_MAIN_COLOR, LV_STATE_FOCUSED);
  lv_obj_align(item.value1, LV_ALIGN_CENTER, 0, 0);

  item.value2 = lv_label_create(item.root);
  lv_label_set_text(item.value2, value2);
  lv_obj_set_style_text_color(item.value2, UI_COLOR_WHITE, 0);
  lv_obj_set_style_text_color(item.value2, UI_MAIN_COLOR, LV_STATE_FOCUSED);
  lv_obj_align(item.value2, LV_ALIGN_RIGHT_MID, -ROW_HORIZONTAL_PADDING, 0);

  return item;
}

// ---- 工具函数 ----
void ui_clear_obj(ui_obj_t *obj) { lv_obj_clean(obj); }
void ui_set_data(ui_obj_t *obj, void *data) { lv_obj_set_user_data(obj, data); }
void *ui_get_data(ui_obj_t *obj) { return lv_obj_get_user_data(obj); }
void ui_show_page(ui_event_cb_t renderer) { if (renderer) renderer(); }
int ui_get_focused_index() {
  ui_obj_t *focused = lvgl_port_menu_group_get_focused();
  if (focused) {
    return lv_obj_get_index(focused);
  }
  return -1;
}
void ui_focus_to(ui_obj_t *obj) {
  if (obj) {
    lv_group_focus_obj(obj);
  }
}
