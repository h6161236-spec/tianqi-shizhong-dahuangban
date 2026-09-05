#pragma once
/**
 * =====================================================================
 *  touch.h —— 触摸手势识别:滑动、点击、热区
 * =====================================================================
 */

#include <stdint.h>

// =====================================================================
//  手势类型
// =====================================================================
enum Gesture : uint8_t {
  GESTURE_NONE = 0,
  GESTURE_TAP,           // 点击
  GESTURE_SWIPE_LEFT,    // 向左滑
  GESTURE_SWIPE_RIGHT,   // 向右滑
  GESTURE_SWIPE_UP,      // 向上滑
  GESTURE_SWIPE_DOWN,    // 向下滑
};

// =====================================================================
//  热区(可点击区域)
// =====================================================================
struct HitBox {
  char id[20];         // 热区ID(自行保存,避免指向渲染函数的临时缓冲区)
  int16_t x, y;
  int16_t w, h;
  void* userData;      // 额外数据
};

#define MAX_HITBOXES 48
extern HitBox g_hitBoxes[MAX_HITBOXES];
extern uint8_t g_hitBoxCount;

// =====================================================================
//  触摸状态
// =====================================================================
struct TouchState {
  bool down;
  int16_t x, y;
  int16_t startX, startY;
  uint32_t startMs;
  char pressedHit[20];      // 正在按下的热区ID
  char tappedHit[20];       // 本次抬起确认的热区ID
};

extern TouchState g_touch;

// =====================================================================
//  接口函数
// =====================================================================

// 触摸采样(需要外部实现XPT2046读取)
bool touchSample(int* x, int* y);

// 手势识别(每帧调用)
Gesture touchUpdate();

// 热区管理
void hitClear();
void hitAdd(const char* id, int16_t x, int16_t y, int16_t w, int16_t h, void* userData = nullptr);
const HitBox* hitFind(int16_t x, int16_t y);

// 检查某个热区是否被按下
bool hitIsPressed(const char* id);

// 检查本次 GESTURE_TAP 对应的热区
bool hitWasTapped(const char* id);
const char* touchTappedId();
