// =====================================================================
//  蓝白界面配色 —— 与网页预览 preview/ui.html 完全一致的 18 色
//  所有颜色都是 RGB565,数值 = rgb565(r,g,b) 编译期算好
//  页面淡蓝底、白色内容区、主蓝交互;皮卡丘精灵调色板独立保留
// =====================================================================
#pragma once
#include <Arduino.h>

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// 24bit hex -> RGB565(供天空等 24 位色临时转换)
constexpr uint16_t hex565(uint32_t h) {
    return rgb565((h >> 16) & 0xFF, (h >> 8) & 0xFF, h & 0xFF);
}

// 通道混合:av/bv 已先掩好。负向差靠无符号回绕 ≈ -d/256,误差 ≤ 1/通道
constexpr uint32_t lerpCh(uint32_t av, uint32_t bv, uint32_t t, uint32_t mask) {
    return (av + (((bv - av) * t) >> 8)) & mask;
}

// 两个 RGB565 颜色线性混合,t=0 完全是 a,t=255 完全是 b
// (C++11 constexpr 只能一条 return,早期分支并成三目)
constexpr uint16_t lerp565(uint16_t a, uint16_t b, uint8_t t) {
    return (t == 0) ? a
         : (t == 255) ? b
         : (uint16_t)((lerpCh((a >> 11) & 0x1F, (b >> 11) & 0x1F, t, 0x1F) << 11) |
                      (lerpCh((a >> 5) & 0x3F, (b >> 5) & 0x3F, t, 0x3F) << 5) |
                      lerpCh(a & 0x1F, b & 0x1F, t, 0x1F));
}

// 向背景色淡出:dim565(前景, 0)= 背景, dim565(前景, 255)= 前景
constexpr uint16_t dim565(uint16_t bg, uint16_t c, uint8_t p) { return lerp565(bg, c, p); }

// ============================ 主题色表 ================================
// (名字 = 预览里的常量名,值 = 预览 hex 经 RGB565 量化,已在网页端对齐)
namespace TH {
constexpr uint16_t BG       = hex565(0xEDF6FD);  // 页面淡蓝底
constexpr uint16_t CARD     = hex565(0xFFFFFF);  // 卡片
constexpr uint16_t CARD_HI  = hex565(0xD9ECFF);  // 浅蓝选中态
constexpr uint16_t RIM      = hex565(0xC8DDED);  // 卡片描边
constexpr uint16_t LINE     = hex565(0xD7E6F2);  // 分割线
constexpr uint16_t ACCENT   = hex565(0x267EC7);  // 主蓝
constexpr uint16_t GOLD_DK  = hex565(0xD9ECFF);  // 旧名称,浅蓝
constexpr uint16_t RED      = hex565(0xD94C54);  // 危险
constexpr uint16_t RED_DK   = hex565(0xFCE7E8);  // 危险浅色
constexpr uint16_t BLUE     = hex565(0x267EC7);  // 信息
constexpr uint16_t GREEN    = hex565(0x31946D);  // 节气/成功
constexpr uint16_t AMBER    = hex565(0xC57C21);  // 高温/警示
constexpr uint16_t PINK     = hex565(0xC65C78);  // 周末
constexpr uint16_t TXT_HI   = hex565(0x1B3650);  // 深蓝标题
constexpr uint16_t TXT      = hex565(0x35546C);  // 正文
constexpr uint16_t TXT_MID  = hex565(0x6D879C);  // 次要文本
constexpr uint16_t TXT_LO   = hex565(0x9FB5C7);  // 弱文本

// ---- 参考工程遗留别名(直接复用的 keyboard/wifi 等模块用旧名字) ----
constexpr uint16_t CYAN     = ACCENT;
constexpr uint16_t PURPLE   = BLUE;
constexpr uint16_t CYAN_DK  = CARD_HI;
constexpr uint16_t PURPLE_DK= CARD_HI;
constexpr uint16_t ROSE     = RED;
constexpr uint16_t ROSE_DK  = RED_DK;
constexpr uint16_t ACCENT_DK= GOLD_DK;

// 强调色按 t(0-255) 从主蓝过渡到蓝青。
inline uint16_t neon(uint8_t t = 128) { return lerp565(ACCENT, hex565(0x45BFD3), t); }
}  // namespace TH

// ========================== 皮卡丘精灵调色板 ==========================
// 与 preview/ui.html 的 SPR_PAL 逐色一致(字符 → 颜色)
namespace PK {
constexpr uint16_t Y = hex565(0xF8D030);  // 皮卡丘黄
constexpr uint16_t L = hex565(0xFCE890);  // 高光黄
constexpr uint16_t D = hex565(0xD69A10);  // 暗黄(脚掌/阴影)
constexpr uint16_t K = hex565(0x282010);  // 黑(耳尖/眼/鼻/嘴)
constexpr uint16_t R = hex565(0xE84028);  // 脸颊红
constexpr uint16_t W = hex565(0xF8F8F0);  // 眼白
constexpr uint16_t B = hex565(0x8C602C);  // 棕(尾根/背纹)
}  // namespace PK
