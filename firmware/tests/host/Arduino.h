#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
uint32_t millis();
struct TestSerial {
  template <typename... Args> void printf(const char*, Args...) {}
};
extern TestSerial Serial;
