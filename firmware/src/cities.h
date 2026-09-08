#pragma once
/**
 * cities.h —— 中国地级行政区 + 直辖市 + 港澳台主要城市
 * 拼音带音节空格,供「suzhou / sz」两种搜索。坐标为市中心约值,够 open-meteo。
 */
#include <stdint.h>

namespace Cities {

struct Rec {
  const char* name;
  const char* py;    // 小写,音节空格分开: "su zhou"
  uint8_t prov;      // PROV_NAMES 下标
  float lat, lon;
};

extern const char* const PROV_NAMES[];
extern const int PROV_N;

extern const Rec LIST[];
extern const int N;

extern const char* const HOT_NAMES[];
extern const int HOT_N;
extern uint16_t HOT_IDX[];   // 对应 LIST 下标,init() 后有效

void init();

/** 精确中文名。没有返回 -1 */
int findName(const char* name);

/**
 * IP 定位回来的名字(中文或英文)对表。
 * 中文精确 / 去「市」再比 / 拼音(去空格,忽略 Shi)。
 */
int findLoose(const char* s);

int matchCount(const char* q);
int matchNth(const char* q, int n);     // 第 n 个匹配的 LIST 下标,-1
int provCount(uint8_t prov);
int provNth(uint8_t prov, int n);

bool pyMatch(const Rec& c, const char* q);

}  // namespace Cities
