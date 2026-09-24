#include <cstdio>
#include <cstring>

#include <Arduino.h>
#include "Preferences.h"
#include "WiFi.h"
#include "wifi_config.h"

MockSerial Serial;
MockWiFi WiFi;
static uint32_t nowMs = 0;
uint32_t millis() { return nowMs; }

static int failures = 0;
static void check(bool condition, const char* name) {
  if (!condition) { ++failures; std::printf("FAIL: %s\n", name); }
}

int main() {
  WifiMgr::init();
  check(WifiMgr::saveConfig("OldNet", "old-password"), "seed saved network");
  WifiMgr::beginProvision("NewNet", "wrong-password");
  check(WifiMgr::provisionResult() == WIFI_PROVISION_CONNECTING,
        "candidate remains uncommitted while connecting");
  nowMs = 16000;
  WifiMgr::service();
  char ssid[WIFI_SSID_CAPACITY], password[WIFI_PASS_CAPACITY];
  WifiMgr::loadConfig(ssid, password);
  check(WifiMgr::provisionResult() == WIFI_PROVISION_FAILED &&
        std::strcmp(ssid, "OldNet") == 0 &&
        std::strcmp(password, "old-password") == 0,
        "wrong password retains old credentials");

  WifiMgr::beginProvision("NewNet", "new-password");
  WiFi.connectAs("OldNet");
  WifiMgr::service();
  WifiMgr::loadConfig(ssid, password);
  check(WifiMgr::provisionResult() == WIFI_PROVISION_FAILED &&
        std::strcmp(ssid, "OldNet") == 0 && !WifiMgr::isConnected(),
        "fallback to another SSID cannot report success");

  WifiMgr::beginProvision("NewNet", "new-password");
  WiFi.connectAs("NewNet");
  WifiMgr::service();
  WifiMgr::loadConfig(ssid, password);
  check(WifiMgr::provisionResult() == WIFI_PROVISION_SUCCESS &&
        std::strcmp(ssid, "NewNet") == 0 &&
        std::strcmp(password, "new-password") == 0 &&
        WifiMgr::isConnected(),
        "matching SSID commits candidate after connection");
  std::printf("WiFi provision: %d failure(s)\n", failures);
  return failures ? 1 : 0;
}
