/**
 * =====================================================================
 *  pika.cpp —— 皮卡丘精灵数据 + 动画
 * =====================================================================
 *  点阵字符:'.'=透明 Y=黄 L=高光黄 D=暗黄 K=黑 R=脸颊红 W=眼白 B=棕
 *  造型:站姿双足朝右奔跑。耳朵向后上方掠,眼/颊/嘴在右端(头部朝右)。
 *  与 preview/ui.html 的 PK_BODY / PK_TAIL / PK_LEGS / PK_ARMS 逐字符一致。
 */

#include "pika.h"
#include "theme.h"

#include <math.h>

namespace {

// ---- 身体+头+耳(24×20,朝右,耳朵向后上方掠) ----
const char BODY[20][25] PROGMEM = {
    ".........KK...KK........",  // r0  耳尖
    ".........KK...KK........",  // r1
    ".........KK...KK........",  // r2
    ".........YY...YY........",  // r3  耳杆
    "..........YY...YY.......",  // r4
    "..........YYYYYYY.......",  // r5  头顶
    ".........YYYYYYYYYY.....",  // r6
    "........YYYYYYYYYYYY....",  // r7
    ".......YYYYYYYYKKYYYY...",  // r8  眼
    "......YYYYYYYYYKWYYYY...",  // r9
    ".....YYYYYYYYYYYYYYYYK..",  // r10 鼻尖
    "...YYYYYYYYYYRRRYYYYKK..",  // r11 脸颊 + 嘴
    "..YYYYYYYYYYYRRRYYYYY...",  // r12
    ".BBYYYYYYYYYYYYYYYYYY...",  // r13 背纹 1
    "BBBYYYYYYYYYYYYYYYYY....",  // r14 背纹 2
    ".YYYYYYYYYYYYYYYYYYY....",  // r15
    ".YYYYYYYYYYYYYYYYYY.....",  // r16
    "..YYYYYYYYYYYYYYYY......",  // r17
    "...YYYYYYYYYYYYYY.......",  // r18 肚
    ".....YYYYYYYYYY.........",  // r19
};

// ---- 闪电尾巴(11×12,右下棕块是尾根,挂在臀部) ----
const char TAIL[12][12] PROGMEM = {
    "..YYYYYY...",  // t0  旗面
    "..YYYYYY...",  // t1
    "..YYYYY....",  // t2
    "...YYY.....",  // t3
    "..YYY......",  // t4
    "..YYY......",  // t5
    "...YYY.....",  // t6
    "....YYY....",  // t7
    ".....YYY...",  // t8
    "......YYY..",  // t9
    ".......BB..",  // t10 棕色尾根
    "........B..",  // t11
};

// ---- 腿(24×4,4 帧奔跑循环:伸展 / 收腿腾空 / 交叉 / 收腿反相) ----
const char LEGS[4][4][25] PROGMEM = {
    {  // f0 全步幅:前爪前探、后爪后蹬,双脚触地
        ".....YYY.....YYY........",
        "....YYY.......YYY.......",
        "...YYY.........YYY......",
        "..DDDD.........DDDD....."},
    {  // f1 收腿腾空(身体抬高 1 精灵px)
        "......YYY..YYY..........",
        "......YYY..YYY..........",
        ".....DDDD..DDDD.........",
        "........................"},
    {  // f2 交叉:两腿剪刀交错
        ".......YYY.YYY..........",
        "......YYY...YYY.........",
        ".....YYY.....YYY........",
        "....DDDD.....DDDD......."},
    {  // f3 收腿(另一相位)
        ".......YYY..YYY.........",
        ".......YYY..YYY.........",
        "......DDDD..DDDD........",
        "........................"},
};

// ---- 前爪(5×2,收在胸前,随步频前后摆) ----
const char ARMS[4][2][6] PROGMEM = {
    {"..YYY", ".YYDD"},
    {".YYY.", ".YDD."},
    {"YYY..", "YDD.."},
    {".YYY.", ".YDD."},
};

// 字符 → 调色板
inline uint16_t palColor(char c) {
  switch (c) {
    case 'Y': return PK::Y;
    case 'L': return PK::L;
    case 'D': return PK::D;
    case 'K': return PK::K;
    case 'R': return PK::R;
    case 'W': return PK::W;
    case 'B': return PK::B;
    default:  return 0;
  }
}

// 画一块点阵到屏(S× 放大),'.' 跳过(透明)
// rowLen = 每行有效字符数(不含结尾 \0);stride = 数组里一行占的字节数
void blitRows(TFT_eSPI& t, int x, int y, const char* rows, int nRows,
              int rowLen, int stride, int S = Pika::PK_SC) {
  for (int r = 0; r < nRows; ++r) {
    const char* line = rows + (size_t)r * stride;
    int runStart = -1;
    uint16_t runC = 0;
    for (int c = 0; c <= rowLen; ++c) {
      const uint16_t col = (c < rowLen) ? palColor(pgm_read_byte(line + c)) : 0;
      const bool solid = (c < rowLen) && col != 0;
      if (solid && runStart < 0) {
        runStart = c;
        runC = col;
      } else if (runStart >= 0 && (!solid || col != runC)) {
        t.fillRect(x + runStart * S, y + r * S, (c - runStart) * S, S, runC);
        runStart = solid ? c : -1;
        if (solid) runC = col;
      }
    }
  }
}

// ---- 动画状态 ----
int   g_frame = 0;       // 跑动帧 0-3(由 Scene 按地面滚动距离设置)
float g_jumpT = 1.0f;    // 跳跃进度,>=1 表示没在跳
constexpr float JUMP_DUR = 0.55f;   // 同预览:dt / .55
constexpr float JUMP_AMP = 26.0f;   // 同预览:sin(PI*t) * 26

}  // namespace

namespace Pika {

void tick(float dt) {
  if (dt <= 0.0f) return;
  if (g_jumpT < 1.0f) g_jumpT += dt / JUMP_DUR;
}

void setFrame(int f) { g_frame = f & 3; }

int frame() { return g_frame; }

void tap() {
  g_jumpT = 0.0f;  // 重新起跳
}

bool jumping() { return g_jumpT < 1.0f; }

float jumpPhase() { return (g_jumpT < 1.0f) ? g_jumpT : -1.0f; }

int draw(TFT_eSPI& t, int x, int y) {
  const int f = g_frame & 3;
  // 腾空帧(f1/f3)身体抬高 1 精灵px,同预览 bob
  const int bob = (f == 1 || f == 3) ? -PK_SC : 0;

  // 跳跃抛物线
  float jy = 0.0f;
  if (g_jumpT < 1.0f) {
    jy = -sinf(PI * g_jumpT) * JUMP_AMP;
    if (jy > 0.0f) jy = 0.0f;  // 浮点误差保护
  }

  const int by = y + bob + (int)(jy - 0.5f * (jy < 0.0f));

  // 尾 → 身体 → 腿 → 前爪(顺序决定遮挡,同预览)
  blitRows(t, x - 9 * PK_SC, by + 5 * PK_SC,  &TAIL[0][0],    12, 11, 12);
  blitRows(t, x,             by,              &BODY[0][0],    20, 24, 25);
  blitRows(t, x,             by + 19 * PK_SC, &LEGS[f][0][0],  4, 24, 25);
  blitRows(t, x + 13 * PK_SC, by + 16 * PK_SC, &ARMS[f][0][0], 2,  5,  6);

  return (int)jy;
}

}  // namespace Pika
