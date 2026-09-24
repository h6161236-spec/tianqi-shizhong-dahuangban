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
#include <string.h>
#include <ctype.h>

#include "cities.h"
#include "config.h"
#include "lunar.h"
#include "utf8_copy.h"
#include "wifi_config.h"

namespace Wx {
namespace {

Data g_wx = {};
Place g_place = {};
bool g_inited = false;
bool g_refreshFailed = false;

bool samePlace(const Data& data, const Place& place) {
  return data.valid && strcmp(data.city, place.name) == 0 &&
         fabsf(data.lat - place.lat) < 0.001f &&
         fabsf(data.lon - place.lon) < 0.001f;
}

bool validCode(int code) {
  switch (code) {
    case 0: case 1: case 2: case 3: case 45: case 48:
    case 51: case 53: case 55: case 56: case 57:
    case 61: case 63: case 65: case 66: case 67:
    case 71: case 73: case 75: case 77:
    case 80: case 81: case 82: case 85: case 86:
    case 95: case 96: case 99: return true;
    default: return false;
  }
}

bool finiteNumber(JsonVariantConst value) {
  return (value.is<int>() || value.is<float>() || value.is<double>()) &&
         isfinite(value.as<double>());
}

bool parseDate(const char* text, int32_t* out) {
  if (!text || strlen(text) != 10 || text[4] != '-' || text[7] != '-') return false;
  for (int i = 0; i < 10; ++i)
    if (i != 4 && i != 7 && !isdigit((uint8_t)text[i])) return false;
  int y = 0, m = 0, d = 0;
  if (sscanf(text, "%4d-%2d-%2d", &y, &m, &d) != 3 ||
      y < 1970 || y > 2099 || m < 1 || m > 12 || d < 1 || d > 31) return false;
  struct tm day = {};
  day.tm_year = y - 1900;
  day.tm_mon = m - 1;
  day.tm_mday = d;
  day.tm_hour = 12;
  const time_t when = mktime(&day);
  if (when < 0 || day.tm_year != y - 1900 || day.tm_mon != m - 1 ||
      day.tm_mday != d) return false;
  *out = (int32_t)when;
  return true;
}

int dateOffset(int32_t date) {
  time_t now = time(nullptr);
  if (now < 1000000000L) return 0;
  struct tm today;
  localtime_r(&now, &today);
  today.tm_hour = 12;
  today.tm_min = today.tm_sec = 0;
  const time_t noon = mktime(&today);
  return (int)lround(difftime((time_t)date, noon) / 86400.0);
}

void defaultPlace() {
  Utf8::copy(g_place.name, sizeof(g_place.name), CFG_CITY_NAME);
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
  Utf8::copy(g_place.name, sizeof(g_place.name), n.c_str());
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
  Utf8::copy(g_wx.city, sizeof(g_wx.city),
           g_place.name[0] ? g_place.name : CFG_CITY_NAME);
  g_wx.lat = g_place.lat;
  g_wx.lon = g_place.lon;
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
  doc["v"] = 2;
  doc["city"] = g_wx.city;
  doc["la"] = g_wx.lat;
  doc["lo"] = g_wx.lon;
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
  if ((doc["v"] | 0) != 2) return false;

  const char* city = doc["city"] | "";
  const float lat = doc["la"] | NAN;
  const float lon = doc["lo"] | NAN;
  if (!city[0] || strlen(city) >= sizeof(g_wx.city) ||
      !isfinite(lat) || !isfinite(lon) || lat < -90 || lat > 90 ||
      lon < -180 || lon > 180 || !validCode(doc["wmo"] | -1) ||
      !finiteNumber(doc["t"]) || !finiteNumber(doc["f"]) ||
      !finiteNumber(doc["h"]) || (doc["h"].as<int>() < 0) ||
      (doc["h"].as<int>() > 100)) return false;
  JsonArray rows = doc["d"].as<JsonArray>();
  if (rows.isNull() || rows.size() != 7) return false;

  Data loaded = {};
  Utf8::copy(loaded.city, sizeof(loaded.city), city);
  loaded.lat = lat;
  loaded.lon = lon;
  loaded.fetchedAt = doc["at"] | 0;
  loaded.wmo = doc["wmo"].as<int>();
  loaded.temp = doc["t"].as<int>();
  loaded.feels = doc["f"].as<int>();
  loaded.hum = doc["h"].as<int>();
  strlcpy(loaded.wind, doc["w"] | "", sizeof(loaded.wind));
  if (!loaded.wind[0]) return false;
  for (JsonArray row : rows) {
    if (row.size() != 4 || !validCode(row[1] | -1) ||
        !finiteNumber(row[2]) || !finiteNumber(row[3]) ||
        row[2].as<int>() < row[3].as<int>()) return false;
    Day& day = loaded.days[loaded.dayN++];
    day.date = row[0] | 0;
    day.wmo = row[1].as<int>();
    day.tmax = row[2].as<int>();
    day.tmin = row[3].as<int>();
    if (day.date <= 0 || (loaded.dayN > 1 &&
        day.date <= loaded.days[loaded.dayN - 2].date)) return false;
  }
  loaded.valid = true;
  g_wx = loaded;
  return true;
}

}  // namespace

void init() {
  g_inited = true;
  Cities::init();
  loadPlace();
  // 顺序:有缓存先用缓存(断网重启也有数);没缓存再上演示数据
  if (!loadCache() && !WifiMgr::getConfig()->saved) makeDemo();
}

const Data& data() {
  if (!g_inited) init();
  static Data visible;
  visible = Data{};
  if (!samePlace(g_wx, g_place)) return visible;
  visible = g_wx;
  visible.dayN = 0;
  for (int i = 0; i < g_wx.dayN; ++i) {
    if (dateOffset(g_wx.days[i].date) >= 0)
      visible.days[visible.dayN++] = g_wx.days[i];
  }
  return visible;
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
  if (name && name[0]) Utf8::copy(g_place.name, sizeof(g_place.name), name);
  g_place.lat = lat;
  g_place.lon = lon;
  g_place.autoMode = !pin;
  savePlace();
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
  if (!isfinite(lat) || !isfinite(lon) || lat < -90.0f || lat > 90.0f ||
      lon < -180.0f || lon > 180.0f) return false;

  const char* city = doc["city"] | "";
  const char* region = doc["regionName"] | "";
  const char* use = (city && city[0]) ? city : region;
  if (!use || !use[0]) use = "当前位置";

  // 名录仅标准化显示名;预测必须使用接口给出的实际坐标。
  const int idx = Cities::findLoose(use);
  if (idx >= 0) {
    const Cities::Rec& c = Cities::LIST[idx];
    Utf8::copy(g_place.name, sizeof(g_place.name), c.name);
    g_place.lat = lat;
    g_place.lon = lon;
  } else {
    Utf8::copy(g_place.name, sizeof(g_place.name), use);
    g_place.lat = lat;
    g_place.lon = lon;
  }
  g_place.autoMode = true;
  savePlace();
  return true;
}

bool fetch() {
  g_refreshFailed = true;
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

  JsonObject current = doc["current"].as<JsonObject>();
  JsonObject daily = doc["daily"].as<JsonObject>();
  if (current.isNull() || daily.isNull() ||
      !validCode(current["weather_code"] | -1) ||
      !finiteNumber(current["temperature_2m"]) ||
      !finiteNumber(current["apparent_temperature"]) ||
      !finiteNumber(current["relative_humidity_2m"]) ||
      !finiteNumber(current["wind_speed_10m"])) return false;
  const char* currentTime = current["time"] | "";
  if (strlen(currentTime) != 16 || currentTime[10] != 'T' ||
      currentTime[13] != ':' || !isdigit((uint8_t)currentTime[11]) ||
      !isdigit((uint8_t)currentTime[12]) ||
      !isdigit((uint8_t)currentTime[14]) ||
      !isdigit((uint8_t)currentTime[15])) return false;
  char currentDay[11];
  memcpy(currentDay, currentTime, 10);
  currentDay[10] = 0;
  int32_t currentDate = 0;
  if (!parseDate(currentDay, &currentDate) ||
      (currentTime[11] - '0') * 10 + currentTime[12] - '0' > 23 ||
      (currentTime[14] - '0') * 10 + currentTime[15] - '0' > 59) return false;
  const double temp = current["temperature_2m"].as<double>();
  const double feels = current["apparent_temperature"].as<double>();
  const double hum = current["relative_humidity_2m"].as<double>();
  const double wind = current["wind_speed_10m"].as<double>();
  if (temp < -100 || temp > 70 || feels < -100 || feels > 80 ||
      hum < 0 || hum > 100 || wind < 0 || wind > 500) return false;
  JsonArray times = daily["time"].as<JsonArray>();
  JsonArray codes = daily["weather_code"].as<JsonArray>();
  JsonArray tmaxs = daily["temperature_2m_max"].as<JsonArray>();
  JsonArray tmins = daily["temperature_2m_min"].as<JsonArray>();
  if (times.size() != 7 || codes.size() != 7 ||
      tmaxs.size() != 7 || tmins.size() != 7) return false;

  Data nx = Data{};
  nx.valid = true;
  nx.demo = false;
  nx.fetchedAt = time(nullptr) > 1000000000L ? (int32_t)time(nullptr) : 0;
  Utf8::copy(nx.city, sizeof(nx.city), g_place.name);
  nx.lat = g_place.lat;
  nx.lon = g_place.lon;
  nx.wmo = current["weather_code"].as<int>();
  nx.temp = (int)lround(temp);
  nx.feels = (int)lround(feels);
  nx.hum = (int)lround(hum);
  snprintf(nx.wind, sizeof(nx.wind), "%d km/h", (int)lround(wind));

  for (int i = 0; i < 7; ++i) {
    const char* ts = times[i] | "";
    Day& day = nx.days[nx.dayN];
    if (!parseDate(ts, &day.date) || !validCode(codes[i] | -1) ||
        !finiteNumber(tmaxs[i]) || !finiteNumber(tmins[i])) return false;
    const double hi = tmaxs[i].as<double>();
    const double lo = tmins[i].as<double>();
    if (hi < lo || hi > 80 || lo < -100 ||
        (i > 0 && day.date <= nx.days[i - 1].date)) return false;
    day.wmo = codes[i].as<int>();
    day.tmax = (int)lround(hi);
    day.tmin = (int)lround(lo);
    ++nx.dayN;
  }

  g_wx = nx;
  saveCache();
  g_refreshFailed = false;
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
    default: return "--";
  }
}

const char* badgeText(char* buf, size_t n) {
  const WiFiConfig* cfg = WifiMgr::getConfig();
  if (!samePlace(g_wx, g_place)) {
    if (WiFi.status() != WL_CONNECTED)
      snprintf(buf, n, cfg && !cfg->saved ? "未配网-暂无数据" : "离线-暂无数据");
    else
      snprintf(buf, n, g_refreshFailed ? "刷新失败-暂无数据" : "暂无天气数据");
    return buf;
  }
  if (cfg && !cfg->saved && !cfg->connected) {
    snprintf(buf, n, g_wx.demo ? "未配网-演示数据" : "未配网-缓存数据");
    return buf;
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (g_wx.demo) {
      snprintf(buf, n, "离线-演示数据");
      return buf;
    }
    if (g_wx.fetchedAt > 0) {
      const long mins = (time(nullptr) - g_wx.fetchedAt) / 60L;
      snprintf(buf, n, "离线-%ld分钟前更新", mins < 0 ? 0L : mins);
    } else {
      snprintf(buf, n, "离线-暂无更新");
    }
    return buf;
  }
  if (g_refreshFailed) {
    snprintf(buf, n, g_wx.demo ? "刷新失败-演示数据" : "刷新失败-使用缓存");
    return buf;
  }
  if (g_wx.demo) {
    snprintf(buf, n, "演示数据");
    return buf;
  }
  if (g_wx.fetchedAt <= 0) {
    snprintf(buf, n, "更新时间未知");
    return buf;
  }
  if (g_wx.fetchedAt > 0 && time(nullptr) - g_wx.fetchedAt > WEATHER_PERIOD_MS / 1000) {
    snprintf(buf, n, "天气已过期");
    return buf;
  }
  return nullptr;
}

const char* dayLabel(int i) {
  const Data& shown = data();
  if (i < 0 || i >= shown.dayN) return "?";
  if (time(nullptr) >= 1000000000L) {
    const int offset = dateOffset(shown.days[i].date);
    if (offset < 0) return "过期";
    if (offset == 0) return "今天";
    if (offset == 1) return "明天";
  }
  time_t t = (time_t)shown.days[i].date;
  struct tm lt;
  localtime_r(&t, &lt);
  return Lunar::weekName(lt.tm_wday);   // "周一"…"周日"
}

}  // namespace Wx
