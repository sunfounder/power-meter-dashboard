#include "button.h"
#include "pin_config.h"

static ButtonId active_key = BUTTON_ID_NONE;
static uint32_t press_start_ms = 0;
static bool long_press_handled = false;
static bool ignore_until_release = false;

// 存储触发的虚拟按键
static VirtualKey triggered_vkey = VKEY_NONE;

void button_init() {
  pinMode(PIN_BUTTON_1, INPUT_PULLUP);
  pinMode(PIN_BUTTON_2, INPUT_PULLUP);
}

void button_loop() {
  // 如果设置了忽略按键，检查按键是否已释放
  if (ignore_until_release) {
    bool p1 = (digitalRead(PIN_BUTTON_1) == LOW);
    bool p2 = (digitalRead(PIN_BUTTON_2) == LOW);
    if (!p1 && !p2) {
      // 按键已释放，恢复检测
      ignore_until_release = false;
      active_key = BUTTON_ID_NONE;
      press_start_ms = 0;
      long_press_handled = false;
    }
    return; // 忽略期间不处理任何按键
  }
  
  // 简单的硬件扫描
  bool p1 = (digitalRead(PIN_BUTTON_1) == LOW);
  bool p2 = (digitalRead(PIN_BUTTON_2) == LOW);
  ButtonId now_key = p1 ? BUTTON_ID_A : (p2 ? BUTTON_ID_B : BUTTON_ID_NONE);

  // 状态机处理
  if (now_key != BUTTON_ID_NONE) {
    if (active_key == BUTTON_ID_NONE) {
      // 刚按下
      active_key = now_key;
      press_start_ms = millis();
      long_press_handled = false;
    } else {
      // 持续按下中，检查是否达到长按时间
      uint32_t dur = millis() - press_start_ms;
      if (dur >= LONG_PRESS_DURATION && !long_press_handled) {
        // 长按时间到，立即触发
        long_press_handled = true;
        if (active_key == BUTTON_ID_A) {
          triggered_vkey = VKEY_BACK;
        } else if (active_key == BUTTON_ID_B) {
          triggered_vkey = VKEY_ENTER;
        }
      }
    }
  } else {
    if (active_key != BUTTON_ID_NONE) {
      // 刚释放，检查是否是短按
      uint32_t dur = millis() - press_start_ms;
      if (dur < LONG_PRESS_DURATION && !long_press_handled) {
        // 短按触发
        if (active_key == BUTTON_ID_A) {
          triggered_vkey = VKEY_UP;
        } else if (active_key == BUTTON_ID_B) {
          triggered_vkey = VKEY_DOWN;
        }
      }
      
      active_key = BUTTON_ID_NONE;
      press_start_ms = 0;
    }
  }
}

VirtualKey button_get_virtual_key() {
  VirtualKey vkey = triggered_vkey;
  triggered_vkey = VKEY_NONE; // 消费事件
  return vkey;
}

void button_clear_state() {
  active_key = BUTTON_ID_NONE;
  press_start_ms = 0;
  long_press_handled = false;
  ignore_until_release = true; // 设置忽略标志，等待按键释放
  triggered_vkey = VKEY_NONE;
}
