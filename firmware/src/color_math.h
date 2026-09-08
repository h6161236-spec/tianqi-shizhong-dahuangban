#pragma once
#include <stdint.h>

namespace ColorMath {
// Mix in RGB888 before quantizing to the panel's RGB565 format.
inline uint32_t mix24(uint32_t a, uint32_t b, float t) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  // A darker destination must produce a negative delta, not unsigned wraparound.
  const int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
  const int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
  return ((uint32_t)(ar + (br - ar) * t) << 16) |
         ((uint32_t)(ag + (bg - ag) * t) << 8) |
         (uint32_t)(ab + (bb - ab) * t);
}
}
