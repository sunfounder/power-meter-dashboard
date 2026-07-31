#pragma once

#include "lvgl.h"

// 使用函数指针类型，保持与 lv_obj_set_user_data 兼容
typedef void (*ui_event_cb_t)(void);
// 自定义对象类型，用于封装 LVGL 对象
typedef lv_obj_t ui_obj_t;

typedef struct {
  ui_obj_t *root;     // 整个行的容器 (row)
  ui_obj_t *label;    // 左侧标题标签
  ui_obj_t *trailing; // 右侧数值标签
} ui_row_label_t;

typedef struct {
  ui_obj_t *root;     // 整个行的容器 (row)
  ui_obj_t *label;    // 左侧标题标签
  ui_obj_t *trailing; // 右侧数值标签
  ui_obj_t *bar;      // 底部的进度条
} ui_row_progress_t;

typedef struct {
  ui_obj_t *root;   // 整个行的容器 (row)
  ui_obj_t *label;  // 左侧标题标签
  ui_obj_t *value1; // 右侧数值标签
  ui_obj_t *value2; // 右侧数值标签
} ui_row_3_col_t;

// 虚拟按键对象
typedef struct {
  ui_obj_t *btn_up;
  ui_obj_t *btn_down;
  ui_obj_t *btn_enter;
  ui_obj_t *btn_back;
} ui_virtual_keys_t;

#define UI_MAIN_COLOR lv_color_hex(0xe27005)      // #e27005
#define UI_MAIN_COLOR_DARK lv_color_hex(0xC26404) // #c26404
#define UI_THIRD_COLOR lv_color_hex(0x222222)

#define UI_COLOR_MAIN lv_color_hex(0xe27005)
#define UI_COLOR_WHITE lv_color_hex(0xDDDDDD)
#define UI_COLOR_LIGHT_GRAY lv_color_hex(0x555555)
#define UI_COLOR_GRAY lv_color_hex(0x555555)
#define UI_COLOR_DARK_GRAY lv_color_hex(0x222222)
#define UI_COLOR_BLACK lv_color_hex(0x050505)

/**
 * @brief 初始化 UI 框架样式
 *
 * @param long_press_duration 长按时间阈值（毫秒）
 */
void ui_init(uint16_t long_press_duration = 1000);
/**
 * @brief 绘制主容器和虚拟按键
 *
 */
void ui_draw_main_container();
/**
 * @brief 根据虚拟按键类型高亮按键
 * @param vkey_type 虚拟按键类型 (1=UP, 2=DOWN, 3=ENTER, 4=BACK)
 */
void ui_highlight_vkey_by_type(int vkey_type);
/**
 * @brief 恢复所有虚拟按键颜色
 */
void ui_restore_all_vkeys();

// ---- UI 组件 ----
/**
 * @brief 创建一个标准页面容器
 *
 * @param parent 父容器
 * @return ui_obj_t* 页面容器对象
 */
ui_obj_t *ui_create_page();
/**
 * @brief 创建一个标准行容器
 *
 * @param parent 父容器
 * @param onclick 点击事件回调函数
 * @return ui_obj_t* 行容器对象
 */
ui_obj_t *ui_create_row(ui_obj_t *parent, ui_event_cb_t onclick = NULL);
/**
 * @brief 创建一个标准页面行标签
 *
 * @param parent 父容器
 * @param title 标题文本
 * @param trailing trailing 文本
 * @param onclick 点击事件回调函数
 * @return ui_row_label_t 标签结构体
 */
ui_row_label_t ui_create_label(ui_obj_t *parent, const char *title,
                               const char *trailing = "",
                               ui_event_cb_t onclick = NULL);
/**
 * @brief 创建一个标准页面行进度条
 *
 * @param parent 父容器
 * @param title 标题文本
 * @param trailing trailing 文本
 * @param min 进度条最小值
 * @param max 进度条最大值
 * @param onclick 点击事件回调函数
 * @return ui_row_progress_t 进度条结构体
 */
ui_row_progress_t ui_create_progress_bar(ui_obj_t *parent, const char *title,
                                         const char *trailing, int min, int max,
                                         ui_event_cb_t onclick = NULL);
/**
 * @brief 创建一个标准页面行三列项
 *
 * @param parent 父容器
 * @param title 标题文本
 * @param value1 第一列数值文本
 * @param value2 第二列数值文本
 * @param onclick 点击事件回调函数
 * @return ui_row_3_col_t 三列项结构体
 */
ui_row_3_col_t ui_create_3_col_item(ui_obj_t *parent, const char *title = "",
                                    const char *value1 = "",
                                    const char *value2 = "",
                                    ui_event_cb_t onclick = NULL);

// ---- 工具函数 ----
/**
 * @brief 清空指定对象的所有子对象
 *
 * @param obj 要清空的对象
 */
void ui_clear_obj(ui_obj_t *obj);
/**
 * @brief 设置指定对象的关联数据
 *
 * @param obj 要设置数据的对象
 * @param data 关联的数据指针
 */
void ui_set_data(ui_obj_t *obj, void *data);
/**
 * @brief 获取指定对象的关联数据
 *
 * @param obj 要获取数据的对象
 * @return void* 关联的数据指针
 */
void *ui_get_data(ui_obj_t *obj);
/**
 * @brief 显示指定页面
 *
 * @param renderer 页面渲染函数
 * @return ui_event_cb_t 回调函数指针
 */
void ui_show_page(ui_event_cb_t renderer);
/**
 * @brief 获取当前聚焦的行索引
 *
 * @return int 当前聚焦的行索引，-1 表示无聚焦行
 */
int ui_get_focused_index();
/**
 * @brief 聚焦到指定对象
 *
 * @param obj 要聚焦的对象
 */
void ui_focus_to(ui_obj_t *obj);
