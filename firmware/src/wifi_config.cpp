/**
 * =====================================================================
 *  wifi_config.cpp —— WiFi配网实现
 * =====================================================================
 */

#include "wifi_config.h"
#include <WiFi.h>
#include <Preferences.h>
#include <Arduino.h>

namespace WifiMgr {

static Preferences prefs;
static WiFiConfig g_config;
static WiFiAP g_scanResults[MAX_SCAN_RESULTS];
static int g_scanCount = 0;
static WifiScanState g_scanState = WIFI_MGR_SCAN_IDLE;

struct SavedNetwork {
  char ssid[WIFI_SSID_CAPACITY];
  char password[WIFI_PASS_CAPACITY];
};

static SavedNetwork g_saved[MAX_SAVED_NETS];
static uint8_t g_savedCount = 0;
static bool g_scanActive = false;
static uint8_t g_scanAttempts = 0;
static uint32_t g_scanStartedMs = 0;
static uint32_t g_scanRetryMs = 0;
static uint32_t g_connectStartedMs = 0;
static uint32_t g_nextAutoScanMs = 0;
static uint8_t g_triedKnownMask = 0;
static bool g_wasConnected = false;
static WifiProvisionResult g_provision = WIFI_PROVISION_IDLE;

static constexpr uint8_t SCAN_MAX_ATTEMPTS = 3;
static constexpr uint32_t SCAN_TIMEOUT_MS = 15000;
static constexpr uint32_t SCAN_RETRY_MS = 500;
static constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;
static constexpr uint32_t AUTO_SCAN_PERIOD_MS = 30000;

static void copyText(char* dst, size_t size, const char* src) {
  if (!dst || size == 0) return;
  strncpy(dst, src ? src : "", size - 1);
  dst[size - 1] = 0;
}

static void persistSavedNetworks() {
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.putUChar("count", g_savedCount);
  for (uint8_t i = 0; i < g_savedCount; ++i) {
    char ssidKey[8], passKey[8];
    snprintf(ssidKey, sizeof(ssidKey), "s%u", i);
    snprintf(passKey, sizeof(passKey), "p%u", i);
    prefs.putString(ssidKey, g_saved[i].ssid);
    prefs.putString(passKey, g_saved[i].password);
  }
  // 保留旧键，回退刷旧固件时仍能使用最近一次网络。
  if (g_savedCount > 0) {
    prefs.putString("ssid", g_saved[0].ssid);
    prefs.putString("password", g_saved[0].password);
  }
  prefs.end();
}

static int knownIndex(const char* ssid) {
  for (uint8_t i = 0; i < g_savedCount; ++i) {
    if (strcmp(g_saved[i].ssid, ssid) == 0) return i;
  }
  return -1;
}

static void connectInternal(const char* ssid, const char* password, int known) {
  char ssidCopy[WIFI_SSID_CAPACITY];
  char passCopy[WIFI_PASS_CAPACITY];
  copyText(ssidCopy, sizeof(ssidCopy), ssid);
  copyText(passCopy, sizeof(passCopy), password);
  copyText(g_config.ssid, sizeof(g_config.ssid), ssidCopy);
  copyText(g_config.password, sizeof(g_config.password), passCopy);
  g_config.saved = known >= 0;
  g_config.connecting = true;
  g_config.connected = false;
  g_connectStartedMs = millis();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssidCopy, passCopy);
  Serial.printf("[wifi] Connecting to %s%s\n", ssidCopy,
                known >= 0 ? " (known)" : "");
}

static void restorePreferredConfig() {
  g_config.ssid[0] = g_config.password[0] = 0;
  g_config.saved = g_savedCount > 0;
  if (g_savedCount > 0) {
    copyText(g_config.ssid, sizeof(g_config.ssid), g_saved[0].ssid);
    copyText(g_config.password, sizeof(g_config.password), g_saved[0].password);
  }
}

static void failProvision() {
  WiFi.disconnect(false, false);
  g_provision = WIFI_PROVISION_FAILED;
  g_config.connecting = false;
  g_config.connected = false;
  restorePreferredConfig();
  g_nextAutoScanMs = millis() + 1000;
  Serial.println("[wifi] Candidate network failed; saved credentials retained");
}

static bool tryNextKnownNetwork() {
  const char* knownSsids[MAX_SAVED_NETS] = {};
  for (uint8_t i = 0; i < g_savedCount; ++i) knownSsids[i] = g_saved[i].ssid;
  const int bestSaved = WifiPolicy::selectBestKnown(
      g_scanResults, g_scanCount, knownSsids, g_savedCount, g_triedKnownMask);
  if (bestSaved < 0) return false;

  g_triedKnownMask |= (1U << bestSaved);
  connectInternal(g_saved[bestSaved].ssid, g_saved[bestSaved].password, bestSaved);
  return true;
}

static void finishScan(int count) {
  g_scanCount = 0;
  for (int i = 0; i < count && g_scanCount < MAX_SCAN_RESULTS; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;

    int duplicate = -1;
    for (int j = 0; j < g_scanCount; ++j) {
      if (strncmp(g_scanResults[j].ssid, ssid.c_str(), WIFI_SSID_CAPACITY) == 0) {
        duplicate = j;
        break;
      }
    }
    if (duplicate >= 0) {
      if (WiFi.RSSI(i) > g_scanResults[duplicate].rssi) {
        g_scanResults[duplicate].rssi = WiFi.RSSI(i);
        g_scanResults[duplicate].channel = WiFi.channel(i);
      }
      continue;
    }

    WiFiAP& result = g_scanResults[g_scanCount++];
    copyText(result.ssid, sizeof(result.ssid), ssid.c_str());
    result.rssi = WiFi.RSSI(i);
    result.secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    result.channel = WiFi.channel(i);
  }
  WiFi.scanDelete();
  g_scanActive = false;
  g_scanState = WIFI_MGR_SCAN_DONE;
  g_triedKnownMask = 0;
  Serial.printf("[wifi] Found %d networks\n", g_scanCount);

  if (WiFi.status() != WL_CONNECTED && !tryNextKnownNetwork()) {
    g_nextAutoScanMs = millis() + AUTO_SCAN_PERIOD_MS;
  }
}

static void scheduleScanRetry(const char* reason) {
  WiFi.scanDelete();
  g_scanActive = false;
  if (g_scanAttempts < SCAN_MAX_ATTEMPTS) {
    g_scanState = WIFI_MGR_SCAN_RUNNING;
    g_scanRetryMs = millis() + SCAN_RETRY_MS;
    Serial.printf("[wifi] Scan %s, retry %u/%u\n", reason,
                  g_scanAttempts + 1, SCAN_MAX_ATTEMPTS);
  } else {
    g_scanState = WIFI_MGR_SCAN_ERROR;
    g_scanCount = 0;
    g_nextAutoScanMs = millis() + AUTO_SCAN_PERIOD_MS;
    Serial.printf("[wifi] Scan failed after %u attempts\n", SCAN_MAX_ATTEMPTS);
  }
}

static void beginScanAttempt() {
  ++g_scanAttempts;
  WiFi.mode(WIFI_STA);
  const int result = WiFi.scanNetworks(true, true);
  if (result == WIFI_SCAN_RUNNING) {
    g_scanActive = true;
    g_scanStartedMs = millis();
    Serial.printf("[wifi] Scan started (attempt %u/%u)\n",
                  g_scanAttempts, SCAN_MAX_ATTEMPTS);
  } else if (result >= 0) {
    finishScan(result);
  } else {
    scheduleScanRetry("could not start");
  }
}

static void pollScan() {
  if (g_scanState != WIFI_MGR_SCAN_RUNNING) return;
  if (!g_scanActive) {
    if ((int32_t)(millis() - g_scanRetryMs) >= 0) beginScanAttempt();
    return;
  }

  const int count = WiFi.scanComplete();
  if (count == WIFI_SCAN_RUNNING) {
    if (millis() - g_scanStartedMs > SCAN_TIMEOUT_MS) {
      scheduleScanRetry("timed out");
    }
  } else if (count >= 0) {
    finishScan(count);
  } else {
    scheduleScanRetry("failed");
  }
}

// =====================================================================
//  初始化
// =====================================================================
void init() {
  g_provision = WIFI_PROVISION_IDLE;
  g_config.saved = false;
  g_config.connecting = false;
  g_config.connected = false;
  g_config.ssid[0] = 0;
  g_config.password[0] = 0;

  g_savedCount = 0;
  bool migratedLegacy = false;
  prefs.begin("wifi", true);
  const uint8_t count = min((int)prefs.getUChar("count", 0), MAX_SAVED_NETS);
  for (uint8_t i = 0; i < count; ++i) {
    char ssidKey[8], passKey[8];
    snprintf(ssidKey, sizeof(ssidKey), "s%u", i);
    snprintf(passKey, sizeof(passKey), "p%u", i);
    const String ssid = prefs.getString(ssidKey, "");
    if (ssid.length() == 0) continue;
    copyText(g_saved[g_savedCount].ssid, sizeof(g_saved[g_savedCount].ssid),
             ssid.c_str());
    copyText(g_saved[g_savedCount].password, sizeof(g_saved[g_savedCount].password),
             prefs.getString(passKey, "").c_str());
    ++g_savedCount;
  }
  if (g_savedCount == 0 && prefs.isKey("ssid")) {
    const String ssid = prefs.getString("ssid", "");
    if (ssid.length() > 0) {
      copyText(g_saved[0].ssid, sizeof(g_saved[0].ssid), ssid.c_str());
      copyText(g_saved[0].password, sizeof(g_saved[0].password),
               prefs.getString("password", "").c_str());
      g_savedCount = 1;
      migratedLegacy = true;
    }
  }
  prefs.end();
  if (migratedLegacy) persistSavedNetworks();

  if (g_savedCount > 0) {
    copyText(g_config.ssid, sizeof(g_config.ssid), g_saved[0].ssid);
    copyText(g_config.password, sizeof(g_config.password), g_saved[0].password);
    g_config.saved = true;
    Serial.printf("[wifi] Loaded %u known network(s), preferred: %s\n",
                  g_savedCount, g_config.ssid);
  }
}

// =====================================================================
//  扫描
// =====================================================================
void startScan() {
  if (g_provision == WIFI_PROVISION_CONNECTING) failProvision();
  if (WiFi.status() != WL_CONNECTED && g_config.connecting) {
    WiFi.disconnect(false, false);
    g_config.connecting = false;
  }
  WiFi.scanDelete();
  g_scanCount = 0;
  g_scanState = WIFI_MGR_SCAN_RUNNING;
  g_scanActive = false;
  g_scanAttempts = 0;
  g_scanRetryMs = millis();
  beginScanAttempt();
}

bool isScanDone() {
  pollScan();
  return g_scanState == WIFI_MGR_SCAN_DONE || g_scanState == WIFI_MGR_SCAN_ERROR;
}

WifiScanState getScanState() {
  pollScan();
  return g_scanState;
}

int getScanResults(WiFiAP* results, int maxCount) {
  int count = g_scanCount < maxCount ? g_scanCount : maxCount;
  for (int i = 0; i < count; i++) {
    results[i] = g_scanResults[i];
  }
  return count;
}

// =====================================================================
//  连接
// =====================================================================
void connect(const char* ssid, const char* password) {
  g_provision = WIFI_PROVISION_IDLE;
  WiFi.scanDelete();
  g_scanActive = false;
  g_scanState = WIFI_MGR_SCAN_IDLE;
  g_triedKnownMask = 0;
  const int known = knownIndex(ssid);
  if (known >= 0) g_triedKnownMask |= (1U << known);
  connectInternal(ssid, password, known);
}

void beginProvision(const char* ssid, const char* password) {
  if (!ssid || !ssid[0]) {
    g_provision = WIFI_PROVISION_FAILED;
    return;
  }
  WiFi.scanDelete();
  g_scanActive = false;
  g_scanState = WIFI_MGR_SCAN_IDLE;
  g_triedKnownMask = 0;
  g_provision = WIFI_PROVISION_CONNECTING;
  WiFi.disconnect(false, false);
  connectInternal(ssid, password, -1);
}

WifiProvisionResult provisionResult() { return g_provision; }

bool isConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    if (g_provision == WIFI_PROVISION_CONNECTING &&
        WiFi.SSID() != g_config.ssid) return false;
    g_config.connected = true;
    g_config.connecting = false;
    return true;
  }
  g_config.connected = false;
  return false;
}

void service() {
  pollScan();
  const uint32_t now = millis();
  if (g_provision == WIFI_PROVISION_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      if (WiFi.SSID() != g_config.ssid) {
        failProvision();
        return;
      }
      char ssid[WIFI_SSID_CAPACITY], password[WIFI_PASS_CAPACITY];
      copyText(ssid, sizeof(ssid), g_config.ssid);
      copyText(password, sizeof(password), g_config.password);
      if (!saveConfig(ssid, password)) {
        failProvision();
        return;
      }
      g_provision = WIFI_PROVISION_SUCCESS;
      Serial.printf("[wifi] Candidate verified: %s\n", ssid);
    } else if (now - g_connectStartedMs > CONNECT_TIMEOUT_MS) {
      failProvision();
      return;
    } else {
      return;
    }
  }
  if (WiFi.status() == WL_CONNECTED) {
    g_config.connected = true;
    g_config.connecting = false;
    g_wasConnected = true;
    g_nextAutoScanMs = now + AUTO_SCAN_PERIOD_MS;
    return;
  }

  g_config.connected = false;
  if (g_wasConnected) {
    g_wasConnected = false;
    g_nextAutoScanMs = now + 1000;
    Serial.println("[wifi] Connection lost, known-network scan scheduled");
  }
  if (g_config.connecting) {
    if (now - g_connectStartedMs <= CONNECT_TIMEOUT_MS) return;
    Serial.printf("[wifi] Connection to %s timed out\n", g_config.ssid);
    WiFi.disconnect(false, false);
    g_config.connecting = false;
    if (tryNextKnownNetwork()) return;
    g_nextAutoScanMs = now + 1000;
  }

  if (g_scanState == WIFI_MGR_SCAN_RUNNING) return;
  if ((int32_t)(now - g_nextAutoScanMs) >= 0 && g_savedCount > 0) {
    Serial.println("[wifi] Offline, scanning for known networks");
    startScan();
  }
}

const char* getIPAddress() {
  static char ip[16];
  if (isConnected()) {
    IPAddress addr = WiFi.localIP();
    snprintf(ip, sizeof(ip), "%d.%d.%d.%d", addr[0], addr[1], addr[2], addr[3]);
    return ip;
  }
  return "0.0.0.0";
}

// =====================================================================
//  NVS存储
// =====================================================================
bool saveConfig(const char* ssid, const char* password) {
  if (!ssid || !ssid[0]) return false;
  const int existing = knownIndex(ssid);
  SavedNetwork saved = {};
  copyText(saved.ssid, sizeof(saved.ssid), ssid);
  copyText(saved.password, sizeof(saved.password), password);
  const int shiftFrom = existing >= 0 ? existing : min((int)g_savedCount, MAX_SAVED_NETS - 1);
  for (int i = shiftFrom; i > 0; --i) g_saved[i] = g_saved[i - 1];
  g_saved[0] = saved;
  if (existing < 0 && g_savedCount < MAX_SAVED_NETS) ++g_savedCount;
  persistSavedNetworks();

  copyText(g_config.ssid, sizeof(g_config.ssid), saved.ssid);
  copyText(g_config.password, sizeof(g_config.password), saved.password);
  g_config.saved = true;

  Serial.printf("[wifi] Saved: %s\n", ssid);
  return true;
}

bool loadConfig(char* ssid, char* password) {
  if (g_savedCount == 0) return false;
  copyText(ssid, WIFI_SSID_CAPACITY, g_saved[0].ssid);
  copyText(password, WIFI_PASS_CAPACITY, g_saved[0].password);
  return true;
}

void clearConfig() {
  g_provision = WIFI_PROVISION_IDLE;
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.end();

  g_config.ssid[0] = 0;
  g_config.password[0] = 0;
  g_config.saved = false;
  g_savedCount = 0;

  Serial.println("[wifi] Config cleared");
}

WiFiConfig* getConfig() {
  return &g_config;
}

}  // namespace WifiMgr
