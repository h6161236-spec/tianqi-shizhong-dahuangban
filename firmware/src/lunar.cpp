#include "lunar.h"

#include <math.h>
#include <string.h>

namespace {

// ---------------------------------------------------------------------
//  农历查表 2020–2049
//
//  每年一个 20 位整数,自低位往高位:
//    bit 0–3    闰月月份(0 = 当年无闰月)
//    bit 4–15   一到十二月的大小月,bit(16-m) 置位 = 该月 30 天,否则 29
//    bit 16     闰月是否 30 天
//
//  例:2020 = 0x07954,低 4 位 0x4 → 闰四月;bit16 = 0 → 闰四月是 29 天。
//      基准日 2020-01-25 = 农历 2020 正月初一。
//
//  要延长范围就往后续,顺便把 L_MAX 改掉。
// ---------------------------------------------------------------------
const uint32_t LUNAR_TAB[] = {
    0x07954, 0x06aa0, 0x0ad50, 0x05b52, 0x04b60, 0x0a6e6, 0x0a4e0, 0x0d260,
    0x0ea65, 0x0d530, 0x05aa0, 0x076a3, 0x096d0, 0x04afb, 0x04ad0, 0x0a4d0,
    0x1d0b6, 0x0d250, 0x0d520, 0x0dd45, 0x0b5a0, 0x056d0, 0x055b2, 0x049b0,
    0x0a577, 0x0a4b0, 0x0aa50, 0x1b255, 0x06d40, 0x0ada0,
};
constexpr int16_t L_MIN = 2020;
constexpr int16_t L_MAX = L_MIN + (int16_t)(sizeof(LUNAR_TAB) / sizeof(LUNAR_TAB[0])) - 1;

/** 农历年的闰月月份,0 = 无闰月 */
inline uint8_t leapMonth(int16_t y) { return (uint8_t)(LUNAR_TAB[y - L_MIN] & 0xF); }

/** 闰月天数,无闰月返回 0 */
inline uint8_t leapDays(int16_t y) {
  if (!leapMonth(y)) return 0;
  return (LUNAR_TAB[y - L_MIN] & 0x10000) ? 30 : 29;
}

/** 农历 y 年第 m 个平月的天数(29 或 30) */
inline uint8_t monthDays(int16_t y, uint8_t m) {
  return (LUNAR_TAB[y - L_MIN] & (0x10000u >> m)) ? 30 : 29;
}

/** 农历一年总天数 */
int yearDays(int16_t y) {
  int sum = 348;  // 12 个月 × 29 天
  const uint32_t info = LUNAR_TAB[y - L_MIN];
  for (uint32_t i = 0x8000; i > 0x8; i >>= 1)
    if (info & i) ++sum;
  return sum + leapDays(y);
}

// ---------------------------------------------------------------------
//  节气
//
//  经典的"回归年 + 偏移表"算法:
//    基准 1900-01-06 02:05:00 UTC,
//    第 n 个节气 = 基准 + 525948.766245 × (年 - 1900) + OFFSET[n]  分钟
//  1900–2100 有效。个别年份会和权威历书差一天(没算岁差和月球扰动)。
//
//  那个 02:05 的零头是算法标定出来的,把北京时区的偏移一并吸收了 ——
//  所以下面全程按 UTC 算,不要"顺手"加 8 小时。
// ---------------------------------------------------------------------
const uint32_t TERM_OFF[TERM_N] = {
        0,  21208,  42467,  63836,  85337, 107014, 128867, 150921,
   173149, 195551, 218072, 240693, 263343, 285989, 308563, 331033,
   353350, 375494, 397447, 419210, 440795, 462224, 483532, 504758,
};
constexpr double MIN_PER_YEAR = 525948.766245;

// ---------------------------------------------------------------------
//  中文串表
// ---------------------------------------------------------------------
const char* const LMON[12] = {"正", "二", "三", "四", "五", "六",
                              "七", "八", "九", "十", "冬", "腊"};
const char* const DIG[10] = {"一", "二", "三", "四", "五",
                             "六", "七", "八", "九", "十"};
const char* const STEM[10] = {"甲", "乙", "丙", "丁", "戊",
                              "己", "庚", "辛", "壬", "癸"};
const char* const BRANCH[12] = {"子", "丑", "寅", "卯", "辰", "巳",
                                "午", "未", "申", "酉", "戌", "亥"};
const char* const ZODIAC[12] = {"鼠", "牛", "虎", "兔", "龙", "蛇",
                                "马", "羊", "猴", "鸡", "狗", "猪"};
const char* const WEEK[7] = {"周日", "周一", "周二", "周三",
                             "周四", "周五", "周六"};
const char* const TERM_NAME[TERM_N] = {
    "小寒", "大寒", "立春", "雨水", "惊蛰", "春分", "清明", "谷雨",
    "立夏", "小满", "芒种", "夏至", "小暑", "大暑", "立秋", "处暑",
    "白露", "秋分", "寒露", "霜降", "立冬", "小雪", "大雪", "冬至",
};

/** 安全拼接,保证 NUL 结尾;满了就停,不截断多字节字符 */
void appendUtf8(char* out, size_t outSize, const char* s) {
  const size_t have = strlen(out);
  const size_t need = strlen(s);
  if (have + need + 1 > outSize) return;  // 放不下整个字符就整个不放
  memcpy(out + have, s, need + 1);
}

}  // namespace

namespace Lunar {

// ---------------------------------------------------------------------
//  公历
// ---------------------------------------------------------------------

bool isLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

uint8_t monthLen(int y, int m) {
  static const uint8_t L[12] = {31, 28, 31, 30, 31, 30,
                                31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12) return 0;
  if (m == 2 && isLeapYear(y)) return 29;
  return L[m - 1];
}

int32_t daysFromCivil(int y, int m, int d) {
  // Howard Hinnant 的算法:把 3 月当年初,闰日就落在"年末",省掉一堆分支。
  // 纯整数,没有 time_t 的 2038 问题,负年份也对。
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = (uint32_t)(y - era * 400);              // 0..399
  const uint32_t doy = (uint32_t)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;  // 0..146096
  return era * 146097 + (int32_t)doe - 719468;  // 719468 = 1970-01-01 的偏移
}

uint8_t weekday(int y, int m, int d) {
  // 1970-01-01 是周四
  const int32_t z = daysFromCivil(y, m, d);
  return (uint8_t)((z % 7 + 11) % 7);
}

// ---------------------------------------------------------------------
//  农历
// ---------------------------------------------------------------------

LunarDate fromSolar(int y, int m, int d) {
  LunarDate r;
  if (y < L_MIN || y > L_MAX) return r;

  // 2020-01-25 = 农历 2020 正月初一
  int32_t off = daysFromCivil(y, m, d) - daysFromCivil(2020, 1, 25);
  if (off < 0) return r;  // 表的起点之前

  int16_t ly = L_MIN;
  for (;;) {
    const int yd = yearDays(ly);
    if (off < yd) break;
    off -= yd;
    if (++ly > L_MAX) return r;  // 表的终点之后
  }

  const uint8_t leap = leapMonth(ly);
  uint8_t lm = 1;
  bool leapNow = false;
  for (;;) {
    const uint8_t md = leapNow ? leapDays(ly) : monthDays(ly, lm);
    if (off < md) break;
    off -= md;
    // 闰月排在对应平月之后:过完平月 leap,先过闰月,再进下一个平月
    if (!leapNow && leap && lm == leap) {
      leapNow = true;
    } else {
      leapNow = false;
      if (++lm > 12) return r;  // 表坏了才会走到这
    }
  }

  r.year = ly;
  r.month = lm;
  r.day = (uint8_t)(off + 1);
  r.leap = leapNow;
  r.valid = true;
  return r;
}

const char* monthName(uint8_t m) {
  return (m >= 1 && m <= 12) ? LMON[m - 1] : "?";
}

const char* dayName(uint8_t d) {
  // 整十有专名,其余按 初/十/廿/三 + 个位
  switch (d) {
    case 10: return "初十";
    case 20: return "二十";
    case 30: return "三十";
    default: break;
  }
  if (d < 1 || d > 30) return "?";
  static const char* const TENS[3] = {"初", "十", "廿"};
  // 拼出来的串要活到调用方用完,用静态缓冲。农历日名同时只会显示一个。
  static char buf[8];
  const uint8_t t = (uint8_t)((d - 1) / 10);   // 0/1/2
  const uint8_t u = (uint8_t)(d - t * 10);     // 1..9
  buf[0] = '\0';
  appendUtf8(buf, sizeof(buf), TENS[t]);
  appendUtf8(buf, sizeof(buf), DIG[u - 1]);
  return buf;
}

void dateText(const LunarDate& l, char* out, size_t outSize) {
  if (!out || outSize == 0) return;
  out[0] = '\0';
  if (!l.valid) {
    appendUtf8(out, outSize, "--");
    return;
  }
  if (l.leap) appendUtf8(out, outSize, "闰");
  appendUtf8(out, outSize, monthName(l.month));
  appendUtf8(out, outSize, "月");
  appendUtf8(out, outSize, dayName(l.day));
}

void stemBranch(int16_t lunarYear, char* out, size_t outSize) {
  if (!out || outSize == 0) return;
  out[0] = '\0';
  if (lunarYear <= 0) return;
  // 公元 4 年是甲子年
  const int s = ((lunarYear - 4) % 10 + 10) % 10;
  const int b = ((lunarYear - 4) % 12 + 12) % 12;
  appendUtf8(out, outSize, STEM[s]);
  appendUtf8(out, outSize, BRANCH[b]);
}

const char* zodiac(int16_t lunarYear) {
  if (lunarYear <= 0) return "?";
  return ZODIAC[((lunarYear - 4) % 12 + 12) % 12];
}

void yearText(int16_t lunarYear, char* out, size_t outSize) {
  if (!out || outSize == 0) return;
  out[0] = '\0';
  if (lunarYear <= 0) return;
  stemBranch(lunarYear, out, outSize);
  appendUtf8(out, outSize, "年 ");
  appendUtf8(out, outSize, zodiac(lunarYear));
}

const char* weekName(uint8_t w) { return w < 7 ? WEEK[w] : "?"; }

// ---------------------------------------------------------------------
//  节气
// ---------------------------------------------------------------------

const char* termName(uint8_t t) { return t < TERM_N ? TERM_NAME[t] : "?"; }

uint8_t termDay(int y, uint8_t t) {
  if (t >= TERM_N || y < 1900 || y > 2100) return 0;

  // 基准 1900-01-06 02:05 UTC,换成"距 1970-01-01 的分钟数"
  const int64_t base = (int64_t)daysFromCivil(1900, 1, 6) * 1440 + 2 * 60 + 5;
  const int64_t mins =
      base + (int64_t)(MIN_PER_YEAR * (y - 1900) + (double)TERM_OFF[t] + 0.5);

  // 向下取整到天(mins 在这个年份范围内恒为正,但还是写成 floor 语义)
  int64_t dayNum = mins / 1440;
  if (mins < 0 && mins % 1440 != 0) --dayNum;

  // 第 t 个节气固定落在公历第 (t/2 + 1) 月,减掉月初就是"几号"
  const int mon = t / 2 + 1;
  const int32_t first = daysFromCivil(y, mon, 1);
  const int32_t dom = (int32_t)dayNum - first + 1;

  // 算出界说明公式或表出了问题,宁可返回 0 让调用方显示 "--"
  if (dom < 1 || dom > monthLen(y, mon)) return 0;
  return (uint8_t)dom;
}

const char* termToday(int y, int m, int d) {
  if (m < 1 || m > 12) return nullptr;
  // 只需查这个月的两个节气
  for (uint8_t t = (uint8_t)((m - 1) * 2); t < (uint8_t)(m * 2); ++t) {
    if (termDay(y, t) == (uint8_t)d) return TERM_NAME[t];
  }
  return nullptr;
}

const char* termNext(int y, int m, int d, int* outDays) {
  if (outDays) *outDays = -1;
  if (m < 1 || m > 12) return nullptr;
  const int32_t today = daysFromCivil(y, m, d);

  // 从本月第一个节气往后扫两年,够跨年
  for (int k = 0; k < TERM_N * 2; ++k) {
    const int idx = (m - 1) * 2 + k;
    const int yy = y + idx / TERM_N;
    const uint8_t t = (uint8_t)(idx % TERM_N);
    const uint8_t dd = termDay(yy, t);
    if (!dd) continue;                       // 这个算不出来,跳过
    const int32_t when = daysFromCivil(yy, t / 2 + 1, dd);
    if (when >= today) {
      if (outDays) *outDays = (int)(when - today);
      return TERM_NAME[t];
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------------
//  节日
// ---------------------------------------------------------------------

const char* festival(const LunarDate& l, int m, int d) {
  // 农历优先 —— 春节比元旦重要
  if (l.valid && !l.leap) {
    switch (l.month) {
      case 1:
        if (l.day == 1) return "春节";
        if (l.day == 15) return "元宵节";
        break;
      case 5:
        if (l.day == 5) return "端午节";
        break;
      case 7:
        if (l.day == 7) return "七夕";
        if (l.day == 15) return "中元节";
        break;
      case 8:
        if (l.day == 15) return "中秋节";
        break;
      case 9:
        if (l.day == 9) return "重阳节";
        break;
      case 12:
        if (l.day == 8) return "腊八";
        if (l.day == 23) return "小年";
        // 除夕 = 腊月最后一天。腊月不会是闰月,直接查平月天数。
        if (l.year >= L_MIN && l.year <= L_MAX &&
            l.day == monthDays(l.year, 12))
          return "除夕";
        break;
      default:
        break;
    }
  }

  // 公历节日
  switch (m) {
    case 1:  if (d == 1)  return "元旦";     break;
    case 2:  if (d == 14) return "情人节";   break;
    case 3:  if (d == 8)  return "妇女节";
             if (d == 12) return "植树节";   break;
    case 5:  if (d == 1)  return "劳动节";
             if (d == 4)  return "青年节";   break;
    case 6:  if (d == 1)  return "儿童节";   break;
    case 7:  if (d == 1)  return "建党节";   break;
    case 8:  if (d == 1)  return "建军节";   break;
    case 9:  if (d == 10) return "教师节";   break;
    case 10: if (d == 1)  return "国庆节";   break;
    case 12: if (d == 25) return "圣诞节";   break;
    default: break;
  }
  return nullptr;
}

}  // namespace Lunar
