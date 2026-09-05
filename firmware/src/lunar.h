#pragma once
/**
 * =====================================================================
 *  lunar.h —— 农历 / 干支生肖 / 二十四节气 / 传统节日
 * =====================================================================
 *  桌面摆件平时是个日历,这些是它该有的信息。全部纯计算,不联网、不占 RAM
 *  (查表都是 const,编译进 Flash 的 DROM 段)。
 *
 *  数据范围
 *    农历查表只覆盖 2020–2049。超出范围 fromSolar() 返回 valid=false,
 *    调用方必须检查 —— 别假设它一定成功。
 *    30 年对一个桌面摆件够了;真要延长就往 LUNAR_TAB 后面续,格式见 .cpp。
 *
 *    节气用的是经典的"回归年 + 偏移表"算法,1900–2100 有效。这算法在个别
 *    年份会和权威历书差一天(它没算岁差和月球扰动),日历显示够用,
 *    别拿它算祭祀吉时。
 */

#include <stddef.h>
#include <stdint.h>

/** 农历日期。用前先看 valid。 */
struct LunarDate {
  int16_t year = 0;    ///< 农历年(春节前算上一年,干支要按这个取)
  uint8_t month = 0;   ///< 1–12
  uint8_t day = 0;     ///< 1–30
  bool leap = false;   ///< 这个月是闰月
  bool valid = false;  ///< 查表成功
};

/** 二十四节气,按公历月份顺序排(每月两个) */
enum SolarTerm : uint8_t {
  TERM_XIAOHAN = 0, TERM_DAHAN,                 // 一月
  TERM_LICHUN, TERM_YUSHUI,                     // 二月
  TERM_JINGZHE, TERM_CHUNFEN,                   // 三月
  TERM_QINGMING, TERM_GUYU,                     // 四月
  TERM_LIXIA, TERM_XIAOMAN,                     // 五月
  TERM_MANGZHONG, TERM_XIAZHI,                  // 六月
  TERM_XIAOSHU, TERM_DASHU,                     // 七月
  TERM_LIQIU, TERM_CHUSHU,                      // 八月
  TERM_BAILU, TERM_QIUFEN,                      // 九月
  TERM_HANLU, TERM_SHUANGJIANG,                 // 十月
  TERM_LIDONG, TERM_XIAOXUE,                    // 十一月
  TERM_DAXUE, TERM_DONGZHI,                     // 十二月
  TERM_N = 24
};

namespace Lunar {

// ---- 公历日期运算 ----

/**
 * 公历日期 → 距 1970-01-01 的天数(可为负)。
 * Howard Hinnant 的 civil_from_days 反函数,不碰 time_t,没有时区和溢出问题。
 */
int32_t daysFromCivil(int y, int m, int d);

/** 星期。0=周日 … 6=周六 */
uint8_t weekday(int y, int m, int d);

/** 该公历月有几天(含闰年判断) */
uint8_t monthLen(int y, int m);

/** 公历闰年 */
bool isLeapYear(int y);

// ---- 农历 ----

/** 公历 → 农历。范围外返回 valid=false。 */
LunarDate fromSolar(int y, int m, int d);

/** 农历月名:"正""二"…"冬""腊"。不含"闰"字和"月"字。m 越界返回 "?" */
const char* monthName(uint8_t m);

/** 农历日名:"初一""十五""廿三""三十"。d 越界返回 "?" */
const char* dayName(uint8_t d);

/**
 * 组合成显示串,如 "四月初八" / "闰四月初八"。
 * @param out 建议 ≥ 20 字节(最长 "闰十一月廿九" = 18 字节 UTF-8 + 结尾)
 */
void dateText(const LunarDate& l, char* out, size_t outSize);

/**
 * 干支 + 生肖,如 "庚子年 鼠"。
 * @param lunarYear 用 LunarDate::year,不是公历年 —— 春节前干支还是上一年的
 */
void yearText(int16_t lunarYear, char* out, size_t outSize);

/** 干支年,如 "庚子"。out ≥ 7 字节 */
void stemBranch(int16_t lunarYear, char* out, size_t outSize);

/** 生肖,如 "鼠"。返回静态串 */
const char* zodiac(int16_t lunarYear);

/** 星期名:"周日""周一"…。w 越界返回 "?" */
const char* weekName(uint8_t w);

// ---- 节气 ----

/** 节气名,如 "清明"。t 越界返回 "?" */
const char* termName(uint8_t t);

/**
 * 第 t 个节气落在公历哪一天。
 * 节气 t 固定在公历第 (t/2 + 1) 月,所以只返回"几号"。
 * @return 1–31;年份超出 1900–2100 返回 0
 */
uint8_t termDay(int y, uint8_t t);

/**
 * 今天是不是节气。
 * @return 节气名,不是则 nullptr
 */
const char* termToday(int y, int m, int d);

/**
 * 下一个节气。
 * @param outDays 距今几天(今天就是节气则为 0)
 * @return 节气名;算不出返回 nullptr
 */
const char* termNext(int y, int m, int d, int* outDays);

// ---- 节日 ----

/**
 * 传统节日 / 公历节日。农历优先(春节比元旦重要)。
 * 除夕要看农历腊月最后一天,所以需要完整的 LunarDate。
 * @return 节日名,没有则 nullptr
 */
const char* festival(const LunarDate& l, int m, int d);

}  // namespace Lunar
