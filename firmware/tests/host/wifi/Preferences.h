#pragma once

#include "Arduino.h"
#include <map>

class Preferences {
 public:
  bool begin(const char* ns, bool) { namespace_ = ns; return true; }
  void end() {}
  void clear() { storage()[namespace_].clear(); }
  void putUChar(const char* key, uint8_t value) {
    storage()[namespace_][key] = std::to_string(value);
  }
  uint8_t getUChar(const char* key, uint8_t fallback = 0) const {
    auto found = storage()[namespace_].find(key);
    return found == storage()[namespace_].end() ? fallback
           : (uint8_t)std::stoi(found->second);
  }
  void putString(const char* key, const char* value) {
    storage()[namespace_][key] = value;
  }
  String getString(const char* key, const char* fallback = "") const {
    auto found = storage()[namespace_].find(key);
    return found == storage()[namespace_].end() ? String(fallback)
           : String(found->second);
  }
  bool isKey(const char* key) const {
    return storage()[namespace_].count(key) != 0;
  }
 private:
  static std::map<std::string, std::map<std::string, std::string>>& storage() {
    static std::map<std::string, std::map<std::string, std::string>> data;
    return data;
  }
  std::string namespace_;
};
