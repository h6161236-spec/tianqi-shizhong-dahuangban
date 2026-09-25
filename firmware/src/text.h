#pragma once
/**
 * =====================================================================
 *  text.h —— 中文文字渲染(u8g2 点阵字体封装)
 * =====================================================================
 *  为什么要包一层:
 *    1. u8g2 的绘制目标是可切换的(屏幕 / 各个 Sprite),裸用容易忘了切,
 *       画到错误的目标上;
 *    2. u8g2 的 y 是基线不是顶边,直接算布局很容易差几个像素;
 *    3. 对齐、截断、跑马灯这些每个页面都要用,不该到处复制。
 *
 *  重要:u8g2 出像素只走 drawFastHLine/drawFastVLine,而这两个在 TFT_eSPI
 *  里是 virtual 的 —— 所以把目标设成 Sprite 时,中文会画进 Sprite 的内存,
 *  这是无闪烁渲染的前提。
 *
 *  字号取舍(objdump 实测):
 *    一套 wqy_t_gb2312 全字库 = 318KB Flash。所以中文只留 12/16 两号,
 *    12 用于状态栏/角标,16 用于标题/歌词/日期。中间号 14 用 16 顶上,
 *    肉眼差别很小,省 318KB。
 *    纯数字场合(温度、秒数)用 t0_*b_tn 点阵数字字,只有几百字节。
 */

#include <TFT_eSPI.h>
#include <stddef.h>
#include <stdint.h>

/**
 * 可用字号。
 *  CN12 / CN16 含全套 GB2312 汉字 + ASCII,各占 318KB Flash。
 *  NUM14 / NUM22 只有 "0-9 : . + -" 等符号,几百字节,给数字用。
 */
enum TxtFont : uint8_t {
  CN12 = 0,   // 12px 中文:状态行、角标、密集信息
  CN16,       // 16px 中文:标题、歌词、日期(正文也用这个)
  NUM14,      // 14px 粗体数字:小号数值
  NUM22,      // 22px 粗体数字:温度、中号数值
  TXT_FONT_N
};

namespace Txt {

/** 上电初始化一次。会预读所有字号的度量值缓存起来。 */
void init(TFT_eSPI& screen);

/** 切换绘制目标(屏幕或某个 Sprite)。后续 draw 都画到这里。 */
void target(TFT_eSPI& t);

/** 当前字体下字符串的像素宽度 */
int width(TxtFont f, const char* s);

/** 字体的行高(ascent-descent)/ 基线以上高度 */
int lineH(TxtFont f);
int ascent(TxtFont f);

/**
 * 左对齐绘制。
 * @param y 基线 y 坐标(不是顶边)
 * @return 绘制结束时的 x
 */
int draw(int x, int y, TxtFont f, uint16_t color, const char* s);

/** 以 topY 为顶边绘制(内部自动换算基线),布局时更好算 */
int drawTop(int x, int topY, TxtFont f, uint16_t color, const char* s);

/** 水平居中于 cx */
int drawCenter(int cx, int y, TxtFont f, uint16_t color, const char* s);

/** 右对齐到 rx */
int drawRight(int rx, int y, TxtFont f, uint16_t color, const char* s);

/**
 * 截断到 maxPx 宽,超出则末尾换成 "..."。
 * 按 UTF-8 字符边界切,不会切出半个汉字。
 */
void clip(TxtFont f, const char* src, int maxPx, char* out, size_t outSize);

/** One full UTF-8 line measured with the hardware font. Returns source bytes consumed. */
size_t wrapLine(TxtFont f, const char* src, int maxPx, char* out, size_t outSize);

/**
 * 跑马灯:在宽 w 的区域里循环滚动 s。
 * 靠 Sprite 边界自动裁剪,所以必须先把 target 设成正好这么宽的 Sprite。
 * 文字比区域窄时不滚动,按 align 摆放(0=左 1=居中 2=右)。
 *
 * @param offset 滚动偏移量,由调用方累加
 * @return 归一化后的 offset,调用方应存回去,防止长跑溢出
 */
int marquee(int y, int w, TxtFont f, uint16_t color, const char* s,
            int offset, uint8_t align = 0);

/**
 * 给 Sprite 左右边缘做淡出遮罩,盖掉跑马灯在边界上被切掉半笔画的毛刺。
 * 必须在文字画完后调用。fadePx 建议 10~14。
 */
void fadeEdges(TFT_eSprite& spr, int w, int h, int fadePx, uint16_t bg);

/** UTF-8 字符数(不是字节数) */
int utf8Len(const char* s);

/** 取前 n 个 UTF-8 字符到 out */
void utf8Head(const char* src, int n, char* out, size_t outSize);

}  // namespace Txt
