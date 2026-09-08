#pragma once
/**
 * =====================================================================
 *  pika.h —— 皮卡丘像素精灵(站姿双足奔跑循环)
 * =====================================================================
 *  点阵数据与 preview/ui.html 的 PK_BODY / PK_TAIL / PK_LEGS / PK_ARMS
 *  逐字符一致:身体+头+耳 24×20、闪电尾 11×12、腿 4 帧 × 24×4、
 *  前爪 4 帧 × 5×2,朝右。屏上按 2× 放大(PK_SC=2)。
 *
 *  动画:4 帧腿循环,步频与地面滚动挂钩(地面走 22px = 一个 4 帧循环,
 *  脚不打滑,由 Scene 统一驱动 setFrame());腾空帧(f1/f3)身体抬高
 *  1 精灵px;点一下原地跳(0.55s 抛物线,幅度 26px)。
 *
 *  数据存 PROGMEM,同色像素合并成一条 fillRect —— 实测最差一帧 61 次填充
 *  (身体 38 + 尾 12 + 腿 8 + 爪 3),加影子和扬尘也在 74 以内,9fps 很轻松。
 *  点阵与预览的一致性由 firmware/tools/check_sprite.py 逐字符校验。
 */

#include <TFT_eSPI.h>

namespace Pika {

// 精灵像素 → 屏幕像素的放大倍数(同预览 PK_SC)
constexpr int PK_SC = 2;

// 精灵尺寸(2× 放大后的屏上尺寸,同预览 PK_W / PK_H)
constexpr int PK_W = 24 * PK_SC;   // 48:身体宽
constexpr int PK_H = 23 * PK_SC;   // 46:身体顶 → 脚底
constexpr int TAIL_W = 11 * PK_SC; // 22
constexpr int TAIL_H = 12 * PK_SC; // 24

// 步频基准:地面滚动速度也用它(同预览 PK_SPD)
constexpr float PK_SPD = 46.0f;

/** 推进跳跃计时。dt 秒;页面切走时可以不管,回来会接着跑 */
void tick(float dt);

/** 设置跑动帧(0-3)。由 Scene 按地面滚动距离算,保证脚不打滑 */
void setFrame(int f);

/** 当前跑动帧 0-3 */
int frame();

/** 触发一次跳跃(点皮卡丘) */
void tap();

/** 是否正在跳(跳起时阴影缩小) */
bool jumping();

/** 跳跃进度 0..1;没在跳返回 -1(给画影子的代码算跳起高度) */
float jumpPhase();

/**
 * 画皮卡丘。(x, y) = 身体左上角,同预览 drawPikachu 的 (x, y)。
 * 直接画屏,不透明填充 —— 背景必须是干净的(全帧重绘时调用)。
 * 影子和扬尘由 Scene 负责(要用地面色),这里只画精灵本体。
 * @return 跳起的 y 偏移(负值 = 在空中),供 Scene 缩放影子
 */
int draw(TFT_eSPI& t, int x, int y);

}  // namespace Pika
