#pragma once

#include "Arduino.h"
#include <vector>

constexpr int WL_CONNECTED = 3;
constexpr int WL_DISCONNECTED = 6;
constexpr int WIFI_STA = 1;
constexpr int WIFI_AUTH_OPEN = 0;
constexpr int WIFI_SCAN_RUNNING = -1;

struct IPAddress {
  int octets[4] = {192, 168, 1, 2};
  int operator[](int index) const { return octets[index]; }
};

struct MockWiFi {
  struct AP { std::string name; int rssi; int encryption; int channel; };
  int connection = WL_DISCONNECTED;
  std::string connectedSsid, requestedSsid, requestedPassword;
  std::vector<AP> aps;
  int scanResult = WIFI_SCAN_RUNNING;

  void mode(int) {}
  void begin(const char* ssid, const char* password) {
    requestedSsid = ssid;
    requestedPassword = password;
    connection = WL_DISCONNECTED;
    connectedSsid.clear();
  }
  void disconnect(bool, bool) {
    connection = WL_DISCONNECTED;
    connectedSsid.clear();
  }
  void connectAs(const char* ssid) {
    connection = WL_CONNECTED;
    connectedSsid = ssid;
  }
  int status() const { return connection; }
  String SSID() const { return connectedSsid; }
  String SSID(int index) const { return aps[index].name; }
  int RSSI(int index) const { return aps[index].rssi; }
  int encryptionType(int index) const { return aps[index].encryption; }
  int channel(int index) const { return aps[index].channel; }
  IPAddress localIP() const { return IPAddress{}; }
  void scanDelete() {}
  int scanNetworks(bool, bool) { return scanResult; }
  int scanComplete() const { return scanResult; }
};
extern MockWiFi WiFi;
