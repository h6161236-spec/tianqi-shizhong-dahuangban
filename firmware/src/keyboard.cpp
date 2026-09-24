/**
 * =====================================================================
 *  keyboard.cpp —— 虚拟键盘实现
 * =====================================================================
 */

#include "keyboard.h"
#include "gfx.h"
#include "text.h"
#include "theme.h"
#include "touch.h"
#include <Arduino.h>
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

namespace Keyboard {

// 键盘按键
static const char* KB_KEYS_LOWER[] = {
  "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
  "q", "w", "e", "r", "t", "y", "u", "i", "o", "p",
  "a", "s", "d", "f", "g", "h", "j", "k", "l", "←",
  "⇧", "z", "x", "c", "v", "b", "n", "m", " ", "."
};

static const char* KB_KEYS_UPPER[] = {
  "!", "@", "#", "$", "%", "^", "&", "*", "(", ")",
  "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P",
  "A", "S", "D", "F", "G", "H", "J", "K", "L", "←",
  "⇧", "Z", "X", "C", "V", "B", "N", "M", "-", "?"
};

static const char* KB_KEYS_NUMBER[] = {
  "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
  "~", "`", "<", ">", "\\", "$", "}", "^", "&", "*",
  "(", ")", "-", "_", "=", "+", "[", "]", "{", "←",
  "⇧", ";", ":", "'", "\"", ",", ".", "/", "?", "|"
};

static bool g_active = false;
static char g_input[KB_MAX_INPUT];
static char g_prompt[64];
static KeyboardLayout g_layout = KB_LOWERCASE;
static uint8_t g_maxChars = KB_MAX_INPUT - 1;

// 键盘布局: 4行x10列
static const int KB_COLS = 10;
static const int KB_ROWS = 4;
static const int KB_KEY_W = 29;
static const int KB_KEY_H = 27;
static const int KB_GAP = 2;
static const int KB_START_Y = 84;

// =====================================================================
//  初始化
// =====================================================================
void init() {
  g_active = false;
  g_input[0] = 0;
  g_prompt[0] = 0;
}

void startInput(const char* prompt, const char* initialValue, uint8_t maxChars) {
  g_active = true;
  g_layout = KB_LOWERCASE;
  g_maxChars = maxChars < KB_MAX_INPUT ? maxChars : KB_MAX_INPUT - 1;
  strlcpy(g_prompt, prompt ? prompt : "", sizeof(g_prompt));
  strlcpy(g_input, initialValue ? initialValue : "", g_maxChars + 1);
}

void endInput() {
  g_active = false;
}

bool isActive() {
  return g_active;
}

const char* getInput() {
  return g_input;
}

void switchLayout(KeyboardLayout layout) {
  g_layout = layout;
}

// =====================================================================
//  渲染
// =====================================================================
void render() {
  if (!g_active) return;

  // 背景遮罩
  tft.fillRect(0, 0, SCR_W, SCR_H, TH::BG);

  // 提示文字
  Gfx::beginStrip(TH::BG);
  Txt::drawCenter(SCR_W / 2, 20, CN16, TH::TXT_HI, g_prompt);
  Gfx::pushStrip(0, 10);

  // 输入框
  Gfx::cardRim(tft, 8, 40, 304, 36, 7, TH::CARD, TH::CYAN, TH::BG);

  // 显示当前输入(截断显示)
  char displayText[KB_MAX_INPUT + 4];
  snprintf(displayText, sizeof(displayText), "%s", g_input);
  const int inputLen = strlen(g_input);
  for (int start = 0; Txt::width(CN16, displayText) > 268 && start < inputLen; ++start)
    snprintf(displayText, sizeof(displayText), "...%s", g_input + start + 1);
  Txt::draw(16, 64, CN16, TH::TXT, displayText);

  // 光标
  int cursorX = 16 + Txt::width(CN16, displayText);
  if ((millis() / 500) % 2 == 0) {  // 闪烁光标
    tft.fillRect(cursorX, 50, 2, 20, TH::CYAN);
  }

  // 键盘按键
  const char** keys;
  switch (g_layout) {
    case KB_UPPERCASE: keys = KB_KEYS_UPPER; break;
    case KB_NUMBER: keys = KB_KEYS_NUMBER; break;
    default: keys = KB_KEYS_LOWER; break;
  }

  int idx = 0;
  for (int row = 0; row < KB_ROWS; row++) {
    for (int col = 0; col < KB_COLS; col++) {
      int x = col * (KB_KEY_W + KB_GAP) + 5;
      int y = KB_START_Y + row * (KB_KEY_H + KB_GAP);

      const char* key = keys[idx++];

      // 特殊键宽度调整
      int keyW = KB_KEY_W;
      if (strcmp(key, "⇧") == 0) {
        keyW = KB_KEY_W;
      }

      // 绘制按键
      bool isSpecial = (strcmp(key, "←") == 0 || strcmp(key, "⇧") == 0);
      uint16_t keyColor = isSpecial ? TH::CARD_HI : TH::CARD;
      uint16_t textColor = TH::TXT;

      Gfx::cardRim(tft, x, y, keyW, KB_KEY_H, 5, keyColor, TH::RIM, TH::BG);

      // 按键文字居中
      const char* caption = strcmp(key, " ") == 0 ? "空格"
                            : strcmp(key, "⇧") == 0 ? "切换" : key;
      int textW = Txt::width(CN12, caption);
      int textX = x + (keyW - textW) / 2;
      Txt::draw(textX, y + 19, CN12, textColor, caption);

      // 注册热区
      char hitId[16];
      snprintf(hitId, sizeof(hitId), "kb%d", idx - 1);
      hitAdd(hitId, x, y, keyW, KB_KEY_H);
    }
  }

  // 独立操作按钮：不再把“确定”伪装成字符键。
  hitAdd("kb.cancel", 8, 204, 148, 30);
  hitAdd("kb.confirm", 164, 204, 148, 30);
  Gfx::cardRim(tft, 8, 204, 148, 30, 7, TH::CARD, TH::RIM, TH::BG);
  Gfx::cardRim(tft, 164, 204, 148, 30, 7, TH::CARD_HI, TH::GREEN, TH::BG);
  Txt::drawCenter(82, 224, CN16, TH::TXT_MID, "取消");
  Txt::drawCenter(238, 224, CN16, TH::TXT_HI, "确定");
}

// =====================================================================
//  触摸处理
// =====================================================================
KeyboardResult handleTouch(int x, int y) {
  if (!g_active) return KB_RESULT_NONE;

  if (hitWasTapped("kb.cancel")) return KB_RESULT_CANCEL;
  if (hitWasTapped("kb.confirm")) return KB_RESULT_CONFIRM;

  const char** keys;
  switch (g_layout) {
    case KB_UPPERCASE: keys = KB_KEYS_UPPER; break;
    case KB_NUMBER: keys = KB_KEYS_NUMBER; break;
    default: keys = KB_KEYS_LOWER; break;
  }

  // 检查热区
  for (int i = 0; i < KB_ROWS * KB_COLS; i++) {
    char hitId[16];
    snprintf(hitId, sizeof(hitId), "kb%d", i);

    if (hitWasTapped(hitId)) {
      const char* key = keys[i];

      if (strcmp(key, "←") == 0) {
        // 退格
        int len = strlen(g_input);
        if (len > 0) g_input[len - 1] = 0;
      } else if (strcmp(key, "⇧") == 0) {
        // 切换布局
        g_layout = (KeyboardLayout)((g_layout + 1) % 3);
      } else {
        // 普通按键
        int len = strlen(g_input);
        if (len < g_maxChars) {
          strncat(g_input, key, 1);
        }
      }
      break;
    }
  }

  return KB_RESULT_NONE;
}

}  // namespace Keyboard
