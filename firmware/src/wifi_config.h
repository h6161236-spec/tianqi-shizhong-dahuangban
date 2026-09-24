#pragma once
/**
 * =====================================================================
 *  wifi_config.h —— WiFi配网管理
 *  功能: 扫描AP、保存配置到NVS、自动连接
 * =====================================================================
 */

#include <stdint.h>

#define WIFI_SSID_CAPACITY 33
#define WIFI_PASS_CAPACITY 65
#define MAX_SCAN_RESULTS 16
#define MAX_SAVED_NETS 5    // 记住的 WiFi 上限(最近使用优先,超了丢最旧的)

// WiFi AP信息
struct WiFiAP {
  char ssid[WIFI_SSID_CAPACITY];
  int rssi;           // 信号强度
  bool secured;       // 是否加密
  uint8_t channel;
};

namespace WifiPolicy {
// 从扫描结果中选择信号最强、且本轮尚未尝试的已知网络索引。
int selectBestKnown(const WiFiAP* aps, int apCount,
                    const char* const* knownSsids, int knownCount,
                    uint8_t triedMask);
}

// WiFi配置状态
struct WiFiConfig {
  char ssid[WIFI_SSID_CAPACITY];
  char password[WIFI_PASS_CAPACITY];
  bool saved;         // 是否已保存到NVS
  bool connecting;    // 正在连接中
  bool connected;     // 已连接
};

enum WifiScanState : uint8_t {
  WIFI_MGR_SCAN_IDLE,
  WIFI_MGR_SCAN_RUNNING,
  WIFI_MGR_SCAN_DONE,
  WIFI_MGR_SCAN_ERROR,
};

enum WifiProvisionResult : uint8_t {
  WIFI_PROVISION_IDLE,
  WIFI_PROVISION_CONNECTING,
  WIFI_PROVISION_SUCCESS,
  WIFI_PROVISION_FAILED,
};

namespace WifiMgr {

// 初始化(从NVS读取已保存的配置)
void init();

// 开始扫描WiFi(异步)
void startScan();

// 在 loop() 中持续调用，推进扫描重试、连接超时和断网自动连接。
void service();

// 检查扫描是否完成
bool isScanDone();

// 区分“确实没有热点”和“扫描失败”。
WifiScanState getScanState();

// 获取扫描结果
int getScanResults(WiFiAP* results, int maxCount);

// 连接到指定WiFi
void connect(const char* ssid, const char* password);

// User-entered credentials are committed only after the requested SSID connects.
void beginProvision(const char* ssid, const char* password);
WifiProvisionResult provisionResult();

// 保存配置到NVS
bool saveConfig(const char* ssid, const char* password);

// 从NVS加载配置
bool loadConfig(char* ssid, char* password);

// 清除已保存的配置
void clearConfig();

// 获取当前配置状态
WiFiConfig* getConfig();

// 获取连接状态
bool isConnected();

// 获取IP地址字符串
const char* getIPAddress();

}  // namespace WifiMgr
