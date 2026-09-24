#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

class TFT_eSPI {
 public:
  TFT_eSPI(int width = 320, int height = 240)
      : w(width), h(height), pixels(width * height, 0),
        clipX(0), clipY(0), clipW(width), clipH(height) {}
  virtual ~TFT_eSPI() = default;

  void clear(uint16_t color = 0) { std::fill(pixels.begin(), pixels.end(), color); }
  void setViewport(int x, int y, int width, int height, bool) {
    clipX = x; clipY = y; clipW = width; clipH = height;
  }
  void resetViewport() { clipX = 0; clipY = 0; clipW = w; clipH = h; }
  void drawPixel(int x, int y, uint16_t color) {
    if (x >= 0 && x < w && y >= 0 && y < h &&
        x >= clipX && x < clipX + clipW &&
        y >= clipY && y < clipY + clipH) pixels[y * w + x] = color;
  }
  uint16_t readPixel(int x, int y) const { return pixels[y * w + x]; }
  void drawFastHLine(int x, int y, int width, uint16_t color) {
    for (int i = 0; i < width; ++i) drawPixel(x + i, y, color);
  }
  void fillRect(int x, int y, int width, int height, uint16_t color) {
    for (int row = 0; row < height; ++row)
      drawFastHLine(x, y + row, width, color);
  }
  void fillCircle(int cx, int cy, int radius, uint16_t color) {
    for (int y = -radius; y <= radius; ++y)
      for (int x = -radius; x <= radius; ++x)
        if (x * x + y * y <= radius * radius)
          drawPixel(cx + x, cy + y, color);
  }
  void fillEllipse(int cx, int cy, int rx, int ry, uint16_t color) {
    for (int y = -ry; y <= ry; ++y)
      for (int x = -rx; x <= rx; ++x)
        if ((long long)x * x * ry * ry + (long long)y * y * rx * rx <=
            (long long)rx * rx * ry * ry)
          drawPixel(cx + x, cy + y, color);
  }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t color) {
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
      drawPixel(x0, y0, color);
      if (x0 == x1 && y0 == y1) break;
      const int e = 2 * error;
      if (e >= dy) { error += dy; x0 += sx; }
      if (e <= dx) { error += dx; y0 += sy; }
    }
  }
  void drawWideLine(float x0, float y0, float x1, float y1,
                    float, uint16_t color, uint16_t) {
    drawLine((int)x0, (int)y0, (int)x1, (int)y1, color);
    drawLine((int)x0 + 1, (int)y0, (int)x1 + 1, (int)y1, color);
  }

  int w, h;
  std::vector<uint16_t> pixels;
 private:
  int clipX, clipY, clipW, clipH;
};

class TFT_eSprite : public TFT_eSPI {
 public:
  TFT_eSprite(int width, int height) : TFT_eSPI(width, height) {}
};
