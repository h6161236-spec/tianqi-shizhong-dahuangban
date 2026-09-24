#pragma once

#include <stdint.h>
#include "config.h"

namespace WeatherSchedule {

struct State {
  static constexpr uint32_t CONNECT_DELAY_MS = 1500;
  static constexpr uint32_t PERIOD_MS = WEATHER_PERIOD_MS;

  bool connected = false;
  bool requested = false;
  uint32_t readyAt = 0;
  uint32_t dueAt = 0;
  uint8_t failures = 0;

  void onConnection(bool up, uint32_t now) {
    connected = up;
    if (up) {
      readyAt = now + CONNECT_DELAY_MS;
      dueAt = readyAt;
      failures = 0;
    } else {
      readyAt = 0;
      dueAt = 0;
    }
  }

  void request() { requested = true; }

  bool due(uint32_t now) const {
    return connected && (int32_t)(now - readyAt) >= 0 &&
           (requested || (int32_t)(now - dueAt) >= 0);
  }

  void started() { requested = false; }

  void success(uint32_t now) {
    failures = 0;
    dueAt = now + PERIOD_MS;
  }

  uint32_t failure(uint32_t now) {
    static const uint32_t BACKOFF_MS[] = {30000, 60000, 120000, 300000};
    const uint8_t idx = failures < 4 ? failures : 3;
    if (failures < 4) ++failures;
    dueAt = now + BACKOFF_MS[idx];
    return BACKOFF_MS[idx];
  }
};

}  // namespace WeatherSchedule
