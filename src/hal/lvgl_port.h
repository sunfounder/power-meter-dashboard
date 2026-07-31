#pragma once

#include "lvgl.h"

/**
 * @brief 初始化 LVGL 适配初始化
 * 初始化按键、LVGL 和通用 UI 样式
 */
void lvgl_port_init();
/**
 * @brief 循环 LVGL 适配循环
 * 处理 LVGL 事件和刷新显示
 */
void lvgl_port_loop();
/**
 * @brief 添加对象到 LVGL 按键组
 * @param obj 要添加的对象指针
 */
void lvgl_port_menu_group_add(lv_obj_t *obj);
/**
 * @brief 聚焦 LVGL 按键组中的下一个对象
 */
void lvgl_port_menu_group_focus_next();
/**
 * @brief 聚焦 LVGL 按键组中的前一个对象
 */
void lvgl_port_menu_group_focus_prev();
/**
 * @brief 获取 LVGL 按键组中的当前聚焦对象
 * @return 当前聚焦对象指针
 */
lv_obj_t *lvgl_port_menu_group_get_focused();
/**
 * @brief 清除 LVGL 按键组中的所有对象
 */
void lvgl_port_menu_group_clear();
