#pragma once
/**
 * =====================================================================
 *  keyboard.h —— 屏幕虚拟键盘
 *  用于WiFi密码输入等场景
 * =====================================================================
 */

#include <stdint.h>

#define KB_MAX_INPUT 65

// 键盘布局
enum KeyboardLayout : uint8_t {
  KB_LOWERCASE = 0,   // 小写字母
  KB_UPPERCASE,       // 大写字母
  KB_NUMBER,          // 数字+符号
};

enum KeyboardResult : uint8_t {
  KB_RESULT_NONE = 0,
  KB_RESULT_CONFIRM,
  KB_RESULT_CANCEL,
};

namespace Keyboard {

// 初始化键盘
void init();

// 开始输入(设置提示文字、初始值)
void startInput(const char* prompt, const char* initialValue = "",
                uint8_t maxChars = KB_MAX_INPUT - 1);

// 结束输入
void endInput();

// 是否正在输入
bool isActive();

// 渲染键盘
void render();

// 处理触摸，明确区分继续输入、确定和取消
KeyboardResult handleTouch(int x, int y);

// 获取当前输入的内容
const char* getInput();

// 切换布局
void switchLayout(KeyboardLayout layout);

}  // namespace Keyboard
