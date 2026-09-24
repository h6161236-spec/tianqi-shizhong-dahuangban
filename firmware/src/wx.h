#pragma once
/**
 * =====================================================================
 *  wx.h —— 天气数据:拉取 / 演示 / NVS 缓存 / 离线角标 / 定位
 * =====================================================================
 *  策略(与网页预览一致):
 *   - 未配网且无可信缓存:演示数据,角标注明;点角标去设置页配网
 *   - 在线:HTTP 拉 open-meteo 7 天预报(不用 TLS,剩余堆跑不动握手),
 *     每 30 分钟刷一次,拉到就写 NVS —— 重启后断网也有数据
 *   - 断网:仅显示地点匹配的 v2 缓存;无匹配数据时显示空状态
 *   - 定位:默认自动。WiFi 连上后用 ip-api(HTTP,中文)认城市,设置里可
 *     改成手选并钉死;自动模式下每次重新连网都会再认一次(带着走)。
 */

#include <stddef.h>
#include <stdint.h>
#include <time.h>

namespace Wx {

/** 一天预报。date = 当天本地正午的 epoch(算「今天/明天/周X」标签用) */
struct Day {
  int32_t date;   // epoch 秒
  int16_t wmo;    // WMO weather_code
  int8_t tmax, tmin;
};

struct Data {
  bool valid;      // 有任何数据(缓存或演示)
  bool demo;       // 是演示数据
  int32_t fetchedAt;  // 拉取时刻 epoch;0 = 时钟还没同步过
  char city[48];
  float lat, lon;  // v2 cache ownership; never relabel another place's data
  int16_t wmo;     // 当前天气代码
  int16_t temp;    // 当前温度 °C
  int16_t feels;   // 体感 °C
  int16_t hum;     // 湿度 %
  char wind[16];   // 风文案:"12 km/h" / 演示 "东南 3 级"
  Day days[7];
  uint8_t dayN;    // 实际天数 0-7
};

/** 当前使用的地点(自动定位或手选钉死) */
struct Place {
  char name[48];
  float lat, lon;
  bool autoMode;   // true = 连网后用 IP 再认一次
};

/** 上电调用:只读取归属明确的 v2 缓存;未配网且无缓存时填演示数据 */
void init();

/** 在线时拉取(open-meteo,阻塞最多 ~15s)。成功返回 true 并写 NVS */
bool fetch();

/** 仅供主动演示模式使用;网络失败不会调用 */
void refreshDemo();

/** 当前地点的快照;地点不匹配时返回 valid=false,并隐藏已过期的预报日 */
const Data& data();

const Place& place();
bool isAuto();

/** 手选城市:pin=true 钉死,之后连网不再自动改 */
void setPlace(const char* name, float lat, float lon, bool pin);

/**
 * 公网 IP 反查城市(ip-api.com,HTTP,中文)。
 * 成功则写入地点(保持 autoMode=true)并返回 true。
 * 失败保留原地点。
 */
bool locate();

/** WMO weather_code → 中文名(28 个全覆盖,预览同表) */
const char* wmoText(int wmo);

/**
 * 离线/未配网/失败/过期角标文案。在线且数据新鲜时返回 nullptr。
 */
const char* badgeText(char* buf, size_t n);

/** 第 i 天的标签:"今天"/"明天"/"周一"…"周日"。越界返回 "?" */
const char* dayLabel(int i);

/** 恢复默认:清 NVS 缓存和定位,回到演示数据 + 自动定位默认城 */
void reset();

}  // namespace Wx
