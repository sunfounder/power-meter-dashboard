#pragma once

#include <Arduino.h>

// 虚拟按键 ID
typedef enum {
  VKEY_NONE = 0,
  VKEY_UP,
  VKEY_DOWN,
  VKEY_ENTER,
  VKEY_BACK
} VirtualKey;

// 物理按键 ID
typedef enum {
  BUTTON_ID_NONE = 0,
  BUTTON_ID_A,
  BUTTON_ID_B
} ButtonId;

// 按键事件
typedef enum {
  BUTTON_EVENT_NONE = 0,
  BUTTON_EVENT_SHORT_PRESS,
  BUTTON_EVENT_LONG_PRESS
} ButtonEvent;

#define LONG_PRESS_DURATION 500 // 长按时间阈值 (ms)

// 初始化按键
void button_init();

// 循环处理按键
void button_loop();

// 获取触发的虚拟按键
VirtualKey button_get_virtual_key();

// 清空按键状态
void button_clear_state();
