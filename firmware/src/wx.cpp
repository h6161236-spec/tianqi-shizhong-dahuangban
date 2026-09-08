/**
 * =====================================================================
 *  wx.cpp —— 天气数据实现
 * =====================================================================
 */

#include "wx.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <math.h>
#include <stdlib.h>

#include "cities.h"
#include "config.h"
#include "lunar.h"
#include "wifi_config.h"

namespace Wx {
namespace {

Data g_wx = {};
Place g_place = {};
bool g_inited = false;

void defaultPlace() {
  strlcpy(g_place.name, CFG_CITY_NAME, sizeof(g_place.name));
  g_place.lat = (float)atof(CFG_LAT);
  g_place.lon = (float)atof(CFG_LON);
  g_place.autoMode = true;
}

void savePlace() {
  Preferences p;
  if (!p.begin(NVS_LOC_NS, false)) return;
  p.putUChar("a", g_place.autoMode ? 1 : 0);
  p.putString("n", g_place.name);
  p.putFloat("la", g_place.lat);
  p.putFloat("lo", g_place.lon);
  p.end();
}

void loadPlace() {
  defaultPlace();
  Preferences p;
  if (!p.begin(NVS_LOC_NS, true)) return;
  const String n = p.getString("n", "");
  const float la = p.getFloat("la", NAN);
  const float lo = p.getFloat("lo", NAN);
  const uint8_t a = p.getUChar("a", 1);
  p.end();
  if (n.isEmpty() || !isfinite(la) || !isfinite(lo)) return;
  if (la < -90.0f || la > 90.0f || lo < -180.0f || lo > 180.0f) return;
  strlcpy(g_place.name, n.c_str(), sizeof(g_place.name));
  g_place.lat = la;
  g_place.lon = lo;
  g_place.autoMode = a != 0;
}

// ---- 演示数据(与预览 makeDemoWeather 同源) ----
void makeDemo() {
  g_wx = Data{};
  g_wx.valid = true;
  g_wx.demo = true;
  g_wx.fetchedAt = time(nullptr) > 1000000000L ? (int32_t)time(nullptr) : 0;
  strncpy(g_wx.city, g_place.name[0] ? g_place.name : CFG_CITY_NAME,
          sizeof(g_wx.city) - 1);
  g_wx.wmo = 2;
  g_wx.temp = 26;
  g_wx.feels = 28;
  g_wx.hum = 64;
  strncpy(g_wx.wind, "东南 3 级", sizeof(g_wx.wind) - 1);

  static const int wm[7] = {0, 1, 2, 2, 3, 61, 80};
  time_t now = time(nullptr);
  if (now < 1000000000L) now = 0;  // 时钟没同步:从 1970 起,标签照样算
  g_wx.dayN = 7;
  for (int i = 0; i < 7; ++i) {
    time_t d = now + (time_t)i * 86400L;
    struct tm lt;
    localtime_r(&d, &lt);
    g_wx.days[i].date = (int32_t)d;
    g_wx.days[i].wmo = wm[(i + lt.tm_mday) % 7];
    g_wx.days[i].tmax = 31 - i + ((i * 7) % 3);
    g_wx.days[i].tmin = 23 - (i % 2);
  }
}

// ---- NVS 缓存 ----
constexpr const char* NVS_KEY = "j";

void saveCache() {
  Preferences prefs;
  if (!prefs.begin(NVS_WX_NS, false)) return;
  JsonDocument doc;
  doc["v"] = 1;
  doc["city"] = g_wx.city;
  doc["at"] = g_wx.fetchedAt;
  doc["wmo"] = g_wx.wmo;
  doc["t"] = g_wx.temp;
  doc["f"] = g_wx.feels;
  doc["h"] = g_wx.hum;
  doc["w"] = g_wx.wind;
  JsonArray arr = doc["d"].to<JsonArray>();
  for (int i = 0; i < g_wx.dayN; ++i) {
    JsonArray row = arr.add<JsonArray>();  // [date, wmo, max, min]
    row.add(g_wx.days[i].date);
    row.add(g_wx.days[i].wmo);
    row.add(g_wx.days[i].tmax);
    row.add(g_wx.days[i].tmin);
  }
  String out;
  serializeJson(doc, out);
  prefs.putString(NVS_KEY, out);
  prefs.end();
}

bool loadCache() {
  Preferences prefs;
  if (!prefs.begin(NVS_WX_NS, true)) return false;
  String s = prefs.getString(NVS_KEY, "");
  prefs.end();
  if (s.isEmpty()) return false;

  JsonDocument doc;
  if (deserializeJson(doc, s)) return false;
  if ((doc["v"] | 0) != 1) return false;

  g_wx = Data{};
  strlcpy(g_wx.city, doc["city"] | "", sizeof(g_wx.city));
  g_wx.fetchedAt = doc["at"] | 0;
  g_wx.wmo = doc["wmo"] | 0;
  g_wx.temp = doc["t"] | 0;
  g_wx.feels = doc["f"] | 0;
  g_wx.hum = doc["h"] | 0;
  strlcpy(g_wx.wind, doc["w"] | "", sizeof(g_wx.wind));
  g_wx.dayN = 0;
  for (JsonArray row : doc["d"].as<JsonArray>()) {
    if (g_wx.dayN >= 7) break;
    g_wx.days[g_wx.dayN].date = row[0] | 0;
    g_wx.days[g_wx.dayN].wmo = row[1] | 0;
    g_wx.days[g_wx.dayN].tmax = row[2] | 0;
    g_wx.days[g_wx.dayN].tmin = row[3] | 0;
    ++g_wx.dayN;
  }
  g_wx.valid = g_wx.dayN > 0;
  g_wx.demo = false;
  return g_wx.valid;
}

}  // namespace

void init() {
  g_inited = true;
  Cities::init();
  loadPlace();
  // 顺序:有缓存先用缓存(断网重启也有数);没缓存再上演示数据
  if (!loadCache()) makeDemo();
  if (g_wx.valid && g_place.name[0])
    strlcpy(g_wx.city, g_place.name, sizeof(g_wx.city));
}

const Data& data() {
  if (!g_inited) init();
  return g_wx;
}

void reset() {
  Preferences prefs;
  if (prefs.begin(NVS_WX_NS, false)) {
    prefs.clear();
    prefs.end();
  }
  if (prefs.begin(NVS_LOC_NS, false)) {
    prefs.clear();
    prefs.end();
  }
  defaultPlace();
  makeDemo();
}

const Place& place() { return g_place; }
bool isAuto() { return g_place.autoMode; }

void setPlace(const char* name, float lat, float lon, bool pin) {
  if (name && name[0]) strlcpy(g_place.name, name, sizeof(g_place.name));
  g_place.lat = lat;
  g_place.lon = lon;
  g_place.autoMode = !pin;
  savePlace();
  strlcpy(g_wx.city, g_place.name, sizeof(g_wx.city));
}

bool locate() {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  WiFiClient client;
  // lang=zh-CN 给中文城市名;fields 裁掉没用的大段
  if (!http.begin(client,
                  "http://ip-api.com/json/?fields=status,message,country,"
                  "regionName,city,lat,lon&lang=zh-CN")) {
    return false;
  }
  http.setConnectTimeout(5000);
  http.setTimeout(7000);
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  const char* st = doc["status"] | "";
  if (strcmp(st, "success") != 0) return false;

  const float lat = doc["lat"] | NAN;
  const float lon = doc["lon"] | NAN;
  if (!isfinite(lat) || !isfinite(lon)) return false;

  const char* city = doc["city"] | "";
  const char* region = doc["regionName"] | "";
  const char* use = (city && city[0]) ? city : region;
  if (!use || !use[0]) use = "当前位置";

  // 能对上名录就用名录的标准名和坐标(open-meteo 更稳);对不上就用接口原值
  const int idx = Cities::findLoose(use);
  if (idx >= 0) {
    const Cities::Rec& c = Cities::LIST[idx];
    strlcpy(g_place.name, c.name, sizeof(g_place.name));
    g_place.lat = c.lat;
    g_place.lon = c.lon;
  } else {
    strlcpy(g_place.name, use, sizeof(g_place.name));
    g_place.lat = lat;
    g_place.lon = lon;
  }
  g_place.autoMode = true;
  savePlace();
  strlcpy(g_wx.city, g_place.name, sizeof(g_wx.city));
  return true;
}

bool fetch() {
  if (WiFi.status() != WL_CONNECTED) return false;

  // current + daily 7 天。不用 hourly —— 页面上没有逐时预报的位子。
  char url[320];
  snprintf(url, sizeof(url),
           "http://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f"
           "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
           "weather_code,wind_speed_10m"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min"
           "&forecast_days=7&timezone=auto",
           (double)g_place.lat, (double)g_place.lon);

  HTTPClient http;
  WiFiClient client;
  if (!http.begin(client, url)) return false;
  http.setConnectTimeout(7000);
  http.setTimeout(9000);
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;

  Data nx = Data{};
  nx.valid = true;
  nx.demo = false;
  nx.fetchedAt = time(nullptr) > 1000000000L ? (int32_t)time(nullptr) : 0;
  strlcpy(nx.city, g_place.name, sizeof(nx.city));
  nx.wmo = doc["current"]["weather_code"] | 0;
  nx.temp = (int)llroundf(doc["current"]["temperature_2m"] | 0.0f);
  nx.feels = (int)llroundf(doc["current"]["apparent_temperature"] | 0.0f);
  nx.hum = doc["current"]["relative_humidity_2m"] | 0;
  snprintf(nx.wind, sizeof(nx.wind), "%d km/h",
           (int)llroundf(doc["current"]["wind_speed_10m"] | 0.0f));

  nx.dayN = 0;
  JsonArray times = doc["daily"]["time"];
  JsonArray codes = doc["daily"]["weather_code"];
  JsonArray tmaxs = doc["daily"]["temperature_2m_max"];
  JsonArray tmins = doc["daily"]["temperature_2m_min"];
  nx.dayN = (int)times.size() < 7 ? (int)times.size() : 7;
  for (int i = 0; i < nx.dayN; ++i) {
    // "2026-09-05" → 当天本地正午 epoch
    const char* ts = times[i] | "";
    int y = 0, m = 0, d = 0;
    if (sscanf(ts, "%d-%d-%d", &y, &m, &d) != 3 || y < 1970) {
      nx.days[i].date = time(nullptr) + (time_t)i * 86400L;
    } else {
      struct tm tmv = {};
      tmv.tm_year = y - 1900;
      tmv.tm_mon = m - 1;
      tmv.tm_mday = d;
      tmv.tm_hour = 12;
      nx.days[i].date = (int32_t)mktime(&tmv);
    }
    nx.days[i].wmo = codes[i] | 0;
    nx.days[i].tmax = (int)llroundf(tmaxs[i] | 0.0f);
    nx.days[i].tmin = (int)llroundf(tmins[i] | 0.0f);
  }
  if (nx.dayN == 0) return false;

  g_wx = nx;
  saveCache();
  return true;
}

void refreshDemo() {
  if (!g_wx.demo) return;
  g_wx.fetchedAt = time(nullptr) > 1000000000L ? (int32_t)time(nullptr) : 0;
  static const int wmos[11] = {0, 1, 2, 2, 3, 45, 51, 61, 63, 71, 95};
  static const int drift[5] = {-1, 0, 0, 1, 2};
  g_wx.wmo = wmos[random(11)];
  int t = g_wx.temp + drift[random(5)];
  if (t < -5) t = -5;
  if (t > 38) t = 38;
  g_wx.temp = t;
  g_wx.feels = t + 2;
  g_wx.hum = 45 + random(40);
  static const int dayWmo[8] = {0, 1, 2, 3, 61, 71, 95, 2};
  for (int i = 0; i < g_wx.dayN; ++i) {
    g_wx.days[i].wmo = dayWmo[(i + random(3)) % 8];
    if (random(10) > 6) g_wx.days[i].tmax += (random(2) ? 1 : -1);
  }
}

const char* wmoText(int w) {
  switch (w) {
    case 0: return "晴";
    case 1: return "基本晴";
    case 2: return "少云";
    case 3: return "阴";
    case 45: return "雾";
    case 48: return "雾凇";
    case 51: return "毛毛雨";
    case 53: return "毛毛雨";
    case 55: return "浓毛毛雨";
    case 56: return "冻毛毛雨";
    case 57: return "冻毛毛雨";
    case 61: return "小雨";
    case 63: return "中雨";
    case 65: return "大雨";
    case 66: return "冻雨";
    case 67: return "强冻雨";
    case 71: return "小雪";
    case 73: return "中雪";
    case 75: return "大雪";
    case 77: return "雪粒";
    case 80: return "小阵雨";
    case 81: return "阵雨";
    case 82: return "强阵雨";
    case 85: return "小阵雪";
    case 86: return "大阵雪";
    case 95: return "雷暴";
    case 96: return "雷暴冰雹";
    case 99: return "强雷暴";
    default: return "—";
  }
}

const char* badgeText(char* buf, size_t n) {
  const WiFiConfig* cfg = WifiMgr::getConfig();
  if (cfg && !cfg->saved && !cfg->connected) {
    snprintf(buf, n, "未配网 · 演示数据");
    return buf;
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (g_wx.fetchedAt > 0) {
      const long mins = (time(nullptr) - g_wx.fetchedAt) / 60L;
      snprintf(buf, n, "离线 · %ld分钟前更新", mins < 0 ? 0L : mins);
    } else {
      snprintf(buf, n, "离线 · 暂无更新");
    }
    return buf;
  }
  return nullptr;  // 在线,不画
}

const char* dayLabel(int i) {
  if (i < 0 || i >= g_wx.dayN) return "?";
  if (i == 0) return "今天";
  if (i == 1) return "明天";
  time_t t = (time_t)g_wx.days[i].date;
  struct tm lt;
  localtime_r(&t, &lt);
  return Lunar::weekName(lt.tm_wday);   // "周一"…"周日"
}

}  // namespace Wx
