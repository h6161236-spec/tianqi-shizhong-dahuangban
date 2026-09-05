/**
 * =====================================================================
 *  touch.cpp —— 触摸手势识别实现
 * =====================================================================
 */

#include "touch.h"
#include "config.h"
#include <Arduino.h>
#include <cmath>

// =====================================================================
//  全局变量
// =====================================================================
TouchState g_touch = {false, 0, 0, 0, 0, 0, "", ""};
HitBox g_hitBoxes[MAX_HITBOXES];
uint8_t g_hitBoxCount = 0;

// =====================================================================
//  热区管理
// =====================================================================
void hitClear() {
  g_hitBoxCount = 0;
}

void hitAdd(const char* id, int16_t x, int16_t y, int16_t w, int16_t h, void* userData) {
  if (g_hitBoxCount >= MAX_HITBOXES) return;
  HitBox& box = g_hitBoxes[g_hitBoxCount++];
  strncpy(box.id, id ? id : "", sizeof(box.id) - 1);
  box.id[sizeof(box.id) - 1] = 0;
  box.x = x;
  box.y = y;
  box.w = w;
  box.h = h;
  box.userData = userData;
}

const HitBox* hitFind(int16_t x, int16_t y) {
  // 从后往前找(后画的在上层)
  for (int i = g_hitBoxCount - 1; i >= 0; --i) {
    const HitBox& h = g_hitBoxes[i];
    if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) {
      return &h;
    }
  }
  return nullptr;
}

bool hitIsPressed(const char* id) {
  return id && g_touch.pressedHit[0] && strcmp(g_touch.pressedHit, id) == 0;
}

bool hitWasTapped(const char* id) {
  return id && g_touch.tappedHit[0] && strcmp(g_touch.tappedHit, id) == 0;
}

const char* touchTappedId() {
  return g_touch.tappedHit;
}

// =====================================================================
//  手势识别
// =====================================================================
Gesture touchUpdate() {
  int x = g_touch.x, y = g_touch.y;
  const bool sampled = touchSample(&x, &y);
  g_touch.tappedHit[0] = 0;

  // 新按下要连续得到两帧相近的有效坐标。第一帧只用于确认，不参与最终
  // 点击坐标，避免落指瞬间的坏点被后续低通长期保留下来。
  static uint8_t armCount = 0;
  static int armX = 0, armY = 0;
  static uint8_t missedSamples = 0;

  if (!g_touch.down) {
    missedSamples = 0;
    if (!sampled) {
      armCount = 0;
      return GESTURE_NONE;
    }

    if (armCount == 0) {
      armX = x;
      armY = y;
      armCount = 1;
      return GESTURE_NONE;
    }

    // 两帧差得太远说明仍在落指/移动，重新从当前稳定候选开始。
    if (abs(x - armX) > 30 || abs(y - armY) > 30) {
      armX = x;
      armY = y;
      return GESTURE_NONE;
    }

    armCount = 0;
    g_touch.down = true;
    g_touch.x = x;  // 使用第二帧，不把首帧误差混进来
    g_touch.y = y;
    g_touch.startX = x;
    g_touch.startY = y;
    g_touch.startMs = millis();

    const HitBox* hit = hitFind(x, y);
    strncpy(g_touch.pressedHit, hit ? hit->id : "", sizeof(g_touch.pressedHit) - 1);
    g_touch.pressedHit[sizeof(g_touch.pressedHit) - 1] = 0;
    Serial.printf("[touch] down x=%d y=%d hit=%s\n", x, y,
                  g_touch.pressedHit[0] ? g_touch.pressedHit : "none");
    return GESTURE_NONE;
  }

  armCount = 0;
  if (sampled) {
    missedSamples = 0;
    // 底层已经做了首样丢弃、压力门限和 closest-two 过滤；这里直接采用
    // 最新稳定坐标，避免原来的 2/3 旧值 IIR 让点击位置滞后。
    g_touch.x = x;
    g_touch.y = y;
    if (millis() - g_touch.startMs < 60) {
      // 落指最初 60ms 不计入滑动距离，但坐标始终使用最新稳定值。
      g_touch.startX = x;
      g_touch.startY = y;
    }
    return GESTURE_NONE;
  }

  // 一帧采样失败可能只是压力或离散度暂时不合格；连续两帧失败才确认抬手。
  if (++missedSamples < 2) return GESTURE_NONE;
  missedSamples = 0;

  const uint32_t duration = millis() - g_touch.startMs;
  const int dx = g_touch.x - g_touch.startX;
  const int dy = g_touch.y - g_touch.startY;
  const int adx = abs(dx);
  const int ady = abs(dy);
  Gesture result = GESTURE_NONE;

  if (duration < SWIPE_MAX_MS) {
    // 电阻屏抬手前最后一帧经常漂十几到几十像素。只要按下时已经命中按钮，
    // 且位移还没有达到明确的滑动阈值，就沿用按下时的热区。
    const bool pressedAControl = g_touch.pressedHit[0] != 0;
    const bool tapMotion =
        (adx < TAP_MAX_PX && ady < TAP_MAX_PX) ||
        (pressedAControl && adx < SWIPE_MIN_PX && ady < SWIPE_MIN_PX);
    if (tapMotion) {
      result = GESTURE_TAP;
      const HitBox* hit = pressedAControl ? nullptr : hitFind(g_touch.x, g_touch.y);
      strncpy(g_touch.tappedHit,
              pressedAControl ? g_touch.pressedHit : (hit ? hit->id : ""),
              sizeof(g_touch.tappedHit) - 1);
      g_touch.tappedHit[sizeof(g_touch.tappedHit) - 1] = 0;
    } else {
      // 滑动:主轴位移要大于阈值,且主轴要明显大于副轴
      if (adx > ady) {
        if (adx > SWIPE_MIN_PX && adx > ady * SWIPE_AXIS_RATIO) {
          result = dx > 0 ? GESTURE_SWIPE_RIGHT : GESTURE_SWIPE_LEFT;
        }
      } else {
        if (ady > SWIPE_MIN_PX && ady > adx * SWIPE_AXIS_RATIO) {
          result = dy > 0 ? GESTURE_SWIPE_DOWN : GESTURE_SWIPE_UP;
        }
      }
    }
  }

  Serial.printf("[touch] up x=%d y=%d dt=%lu dx=%d dy=%d gesture=%d tap=%s\n",
                g_touch.x, g_touch.y, (unsigned long)duration, dx, dy,
                (int)result, g_touch.tappedHit[0] ? g_touch.tappedHit : "none");

  g_touch.down = false;
  g_touch.pressedHit[0] = 0;
  return result;
}
