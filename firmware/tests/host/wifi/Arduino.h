#pragma once

#include <stdint.h>
#include <algorithm>
#include <string>

class String {
 public:
  String(const char* value = "") : value_(value) {}
  String(const std::string& value) : value_(value) {}
  size_t length() const { return value_.length(); }
  const char* c_str() const { return value_.c_str(); }
  bool operator!=(const char* other) const { return value_ != other; }
 private:
  std::string value_;
};

struct MockSerial {
  template <typename... Args> void printf(const char*, Args...) {}
  void println(const char*) {}
};
extern MockSerial Serial;

uint32_t millis();
template <typename T> T min(T a, T b) { return std::min(a, b); }
