#include <stdio.h>
#include <string.h>
#include "Arduino.h"
#include "color_math.h"
#include "lunar.h"
#include "touch.h"
#include "utf8_copy.h"

TestSerial Serial;
static uint32_t now;
static bool down;
static int px, py, failures;
uint32_t millis() { return now; }
bool touchSample(int* x, int* y) { *x = px; *y = py; return down; }
void check(bool pass, const char* name) {
  if (!pass) { if (failures < 10) printf("FAIL: %s\n", name); ++failures; }
}
Gesture sample(uint32_t at, bool pressed, int x, int y) {
  now = at; down = pressed; px = x; py = y;
  return touchUpdate();
}
void reset(uint32_t at) {
  sample(at, false, 0, 0); sample(at + 20, false, 0, 0);
  sample(at + 40, false, 0, 0); hitClear();
}
int main() {
  check(ColorMath::mix24(0xFFFFFF, 0, 0.5f) == 0x7F7F7F, "descending RGB channels");
  check(ColorMath::mix24(0xFF0000, 0x0000FF, 0.5f) == 0x7F007F, "mixed channel directions");
  check(ColorMath::mix24(0x123456, 0xFEDCBA, -1) == 0x123456, "clamped start");
  check(ColorMath::mix24(0xFEDCBA, 0x123456, 2) == 0x123456, "clamped end");
  char shortName[5];
  Utf8::copy(shortName, sizeof(shortName), "苏州");
  check(strcmp(shortName, "苏") == 0, "UTF-8 capacity never cuts a Han character");
  char byteLimited[12];
  Utf8::copy(byteLimited, sizeof(byteLimited), "克孜勒苏", 7);
  check(strcmp(byteLimited, "克孜") == 0,
        "UTF-8 byte limit never cuts a Han character");
  const char truncated[] = {char(0xF0), 0};
  check(Utf8::charBytes(truncated) == 1, "truncated UTF-8 traversal stays in buffer");
  Utf8::copy(byteLimited, sizeof(byteLimited), truncated);
  check(byteLimited[0] == 0, "truncated UTF-8 is not copied");
  check(Utf8::charBytes("歌") == 3 && Utf8::charBytes("A") == 1,
        "mixed CJK/Latin character boundaries");
  const LunarDate endTenth = Lunar::fromSolar(2048, 12, 4);
  check(endTenth.valid && endTenth.year == 2048 && endTenth.month == 10 &&
        endTenth.day == 29, "HKO 2048 tenth month is 29 days");
  const LunarDate startEleventh = Lunar::fromSolar(2048, 12, 5);
  check(startEleventh.valid && startEleventh.year == 2048 &&
        startEleventh.month == 11 && startEleventh.day == 1,
        "HKO 2048 eleventh month starts Dec 5");
  const LunarDate startTwelfth = Lunar::fromSolar(2049, 1, 4);
  check(startTwelfth.valid && startTwelfth.year == 2048 &&
        startTwelfth.month == 12 && startTwelfth.day == 1,
        "HKO 2048 twelfth month starts Jan 4");
  for (int a = 0; a < 256; a += 17) for (int b = 0; b < 256; b += 17) {
    for (int k = 0; k <= 16; ++k) {
      const int expected = (int)(a + (b - a) * (k / 16.0f));
      check(ColorMath::mix24(a * 0x010101u, b * 0x010101u, k / 16.0f)
            == expected * 0x010101u, "RGB channel bounds/interpolation");
    }
  }
  reset(0);
  sample(100, true, 260, 100); sample(210, true, 175, 103);
  sample(320, true, 80, 106); sample(430, false, 80, 106);
  check(sample(540, false, 80, 106) == GESTURE_SWIPE_LEFT, "clock slow-frame left swipe");
  reset(600);
  sample(700, true, 70, 100); sample(720, true, 90, 100);
  sample(740, true, 135, 100); sample(760, false, 135, 100);
  check(sample(780, false, 135, 100) == GESTURE_SWIPE_RIGHT, "fast swipe retains first origin");
  reset(800);
  hitAdd("m.play", 134, 152, 52, 52);
  sample(900, true, 160, 178); sample(920, true, 162, 179);
  sample(940, true, 190, 185); sample(960, false, 190, 185);
  check(sample(980, false, 190, 185) == GESTURE_TAP &&
        strcmp(touchTappedId(), "m.play") == 0, "button release drift retains target");
  reset(1000);
  sample(1100, true, 260, 100); sample(1210, true, 180, 100);
  sample(1900, true, 60, 100); sample(2050, false, 60, 100);
  check(sample(2200, false, 60, 100) == GESTURE_SWIPE_LEFT, "release delay excluded from swipe duration");
  reset(2300);
  sample(2400, true, 20, 20); sample(2420, true, 300, 200);
  check(!g_touch.down, "reject isolated coordinate jump");
  reset(2500);
  printf("host regression: %d failure(s)\n", failures);
  return failures ? 1 : 0;
}
