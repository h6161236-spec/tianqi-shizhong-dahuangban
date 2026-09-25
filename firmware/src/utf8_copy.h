#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace Utf8 {

// Never read past a truncated sequence. Invalid input consumes one byte.
inline size_t charBytes(const char* src) {
  if (!src || !*src) return 0;
  const uint8_t lead = (uint8_t)*src;
  const size_t n = lead < 0x80 ? 1 : lead >= 0xC2 && lead <= 0xDF ? 2
                   : lead >= 0xE0 && lead <= 0xEF ? 3
                   : lead >= 0xF0 && lead <= 0xF4 ? 4 : 1;
  for (size_t i = 1; i < n; ++i)
    if (!src[i] || ((uint8_t)src[i] & 0xC0) != 0x80) return 1;
  return n;
}

inline size_t copy(char* out, size_t capacity, const char* src,
                   size_t maxBytes = (size_t)-1) {
  if (!capacity) return 0;
  if (!src) src = "";
  size_t used = 0;
  while (used < maxBytes && src[used]) {
    const uint8_t lead = (uint8_t)src[used];
    const size_t width = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2
                       : (lead & 0xF0) == 0xE0 ? 3
                       : (lead & 0xF8) == 0xF0 ? 4 : 1;
    if (used + width >= capacity || used + width > maxBytes) break;
    bool complete = true;
    for (size_t i = 1; i < width; ++i)
      if (!src[used + i] || ((uint8_t)src[used + i] & 0xC0) != 0x80) {
        complete = false;
        break;
      }
    if (!complete) break;
    memcpy(out + used, src + used, width);
    used += width;
  }
  out[used] = 0;
  return used;
}

}  // namespace Utf8
