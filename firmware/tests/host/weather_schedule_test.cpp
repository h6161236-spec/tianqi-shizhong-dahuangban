#include "weather_schedule.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main() {
  WeatherSchedule::State s;
  s.onConnection(true, 1000);
  assert(!s.due(2499));
  assert(s.due(2500));
  s.started();
  s.success(3000);
  assert(!s.due(3000 + WEATHER_PERIOD_MS - 1));
  assert(s.due(3000 + WEATHER_PERIOD_MS));

  uint32_t now = 3000 + WEATHER_PERIOD_MS;
  const uint32_t retry[] = {30000, 60000, 120000, 300000, 300000};
  for (uint32_t expected : retry) {
    s.started();
    assert(s.failure(now) == expected);
    assert(!s.due(now + expected - 1));
    assert(s.due(now + expected));
    now += expected;
  }

  s.onConnection(false, now);
  s.request();
  assert(!s.due(now + 10000));
  s.onConnection(true, now + 10000);
  assert(s.failures == 0);
  assert(!s.due(now + 11499));
  assert(s.due(now + 11500));
  s.started();
  s.success(now + 11500);
  s.request();
  assert(s.due(now + 11501));

  const uint32_t wrap = 0xFFFFFF00u;
  s.onConnection(true, wrap);
  assert(!s.due(wrap + 1499u));
  assert(s.due(wrap + 1500u));

  puts("weather schedule: 0 failure(s)");
  return 0;
}
