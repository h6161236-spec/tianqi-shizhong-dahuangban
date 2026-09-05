#pragma once
/**
 * =====================================================================
 *  wx.h —— 天气数据:拉取 / 演示 / NVS 缓存 / 离线角标
 * =====================================================================
 *  策略(与网页预览一致):
 *   - 未配网:演示数据,角标「未配网 · 演示数据」,点了去设置页配网
 *   - 在线:HTTP 拉 open-meteo 7 天预报(不用 TLS,剩余堆跑不动握手),
 *     每 30 分钟刷一次,拉到就写 NVS —— 重启后断网也有数据
 *   - 断网:继续用缓存,角标「离线 · X分钟前更新」;时钟照走(板载 RTC)
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
  char city[20];
  int16_t wmo;     // 当前天气代码
  int16_t temp;    // 当前温度 °C
  int16_t feels;   // 体感 °C
  int16_t hum;     // 湿度 %
  char wind[16];   // 风文案:"12 km/h" / 演示 "东南 3 级"
  Day days[7];
  uint8_t dayN;    // 实际天数 0-7
};

/** 上电调用:读 NVS 缓存;没有缓存且未配网则填演示数据 */
void init();

/** 在线时拉取(open-meteo,阻塞最多 ~15s)。成功返回 true 并写 NVS */
bool fetch();

/** 演示数据轻微漂移(像真的重新拉了一次) */
void refreshDemo();

const Data& data();

/** WMO weather_code → 中文名(28 个全覆盖,预览同表) */
const char* wmoText(int wmo);

/**
 * 离线/未配网角标文案。在线返回 nullptr(不画)。
 * unconfig → "未配网 · 演示数据";offline → "离线 · X分钟前更新"
 */
const char* badgeText(char* buf, size_t n);

/** 第 i 天的标签:"今天"/"明天"/"周一"…"周日"。越界返回 "?" */
const char* dayLabel(int i);

/** 恢复默认:清 NVS 缓存,回到演示数据 */
void reset();

}  // namespace Wx
