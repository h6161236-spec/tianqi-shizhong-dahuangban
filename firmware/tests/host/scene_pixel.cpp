#include <cstdio>
#include <cstdlib>
#include "scene.h"
#include "gfx.h"
#include "pika.h"

namespace {
TFT_eSPI* panelTarget = nullptr;
TFT_eSprite stripBuffer(320, 28);
TFT_eSprite discBuffer(128, 132);
int pikaFrame = 0;
int failures = 0;
int stripTransfers = 0;

void check(bool pass, const char* name) {
  if (!pass) { ++failures; std::printf("FAIL: %s\n", name); }
}

void compareFull(TFT_eSPI& incremental, Scene::Group group, float hour,
                 float moon, const char* name) {
  TFT_eSPI expected;
  panelTarget = &expected;
  Scene::invalidateCache();
  Scene::draw(expected, hour, group, 0.0f, moon, true);
  int mismatches = 0;
  for (int y = 18; y < 240; ++y) for (int x = 0; x < 320; ++x) {
    if (incremental.readPixel(x, y) != expected.readPixel(x, y)) ++mismatches;
  }
  if (mismatches) std::printf("%s: %d differing pixels\n", name, mismatches);
  check(mismatches == 0, name);
  panelTarget = &incremental;
}

void frames(TFT_eSPI& panel, Scene::Group group, float hour, int count,
            float dt = 0.1f) {
  panelTarget = &panel;
  for (int i = 0; i < count; ++i)
    Scene::draw(panel, hour, group, dt, 0.5f, true);
}
}  // namespace

namespace Gfx {
TFT_eSprite& strip() { return stripBuffer; }
TFT_eSprite& disc() { return discBuffer; }
void pushStripRegion(int x, int y, int width, int height, int srcX, int srcY) {
  ++stripTransfers;
  for (int row = 0; row < height; ++row)
    for (int col = 0; col < width; ++col)
      panelTarget->drawPixel(x + col, y + row,
                        stripBuffer.readPixel(srcX + col, srcY + row));
}
void pushDiscRegion(int, int, int, int, int, int) {}
void dots(TFT_eSPI& t, int cx, int y, int n, int cur,
          uint16_t on, uint16_t off, uint16_t) {
  const int x0 = cx - (n - 1) * 14 / 2;
  for (int i = 0; i < n; ++i) {
    if (i == cur) t.fillRect(x0 + i * 14 - 5, y - 2, 11, 4, on);
    else t.fillCircle(x0 + i * 14, y, 2, off);
  }
}
}

namespace Pika {
void tick(float) {}
void setFrame(int frame) { pikaFrame = frame; }
int frame() { return pikaFrame; }
void tap() {}
bool jumping() { return false; }
float jumpPhase() { return -1.0f; }
int draw(TFT_eSPI& t, int x, int y) {
  t.fillRect(x - 18, y + 10, 18, 24, 0xFFE0);
  t.fillRect(x, y, PK_W, PK_H, 0xFFE0);
  t.fillRect(x + 12, y + 16, 4, 4, 0x0000);
  return 0;
}
}

int main() {
  {
    TFT_eSPI oldErase(320, 240);
    const float slant = 0.33f;
    oldErase.drawLine(100, 100, (int)(100 - slant * 4), 93, 0xFFFF);
    for (int k = 0; k < 8; ++k)
      oldErase.drawFastHLine(100 - (int)(slant * 4 * k / 7),
                             100 - k, 2, 0);
    int residue = 0;
    for (int y = 92; y <= 101; ++y)
      for (int x = 96; x <= 103; ++x)
        residue += oldErase.readPixel(x, y) != 0;
    check(residue > 0, "legacy rain erase geometry reproduces residue");
  }
  const int codes[] = {0,1,2,3,45,48,51,53,55,56,57,61,63,65,
                       66,67,71,73,75,77,80,81,82,85,86,95,96,99};
  const Scene::Group groups[] = {
      Scene::GR_CLEAR, Scene::GR_MOSTLY, Scene::GR_PARTLY,
      Scene::GR_OVERCAST, Scene::GR_FOG, Scene::GR_FOG,
      Scene::GR_DRIZZLE, Scene::GR_DRIZZLE, Scene::GR_DRIZZLE,
      Scene::GR_FREEZING, Scene::GR_FREEZING,
      Scene::GR_RAIN, Scene::GR_RAIN, Scene::GR_HEAVYRAIN,
      Scene::GR_FREEZING, Scene::GR_FREEZING,
      Scene::GR_SNOW, Scene::GR_SNOW, Scene::GR_HEAVYSNOW,
      Scene::GR_SNOW, Scene::GR_RAIN, Scene::GR_RAIN,
      Scene::GR_HEAVYRAIN, Scene::GR_SNOW, Scene::GR_HEAVYSNOW,
      Scene::GR_THUNDER, Scene::GR_THUNDER, Scene::GR_THUNDER};
  for (unsigned i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i)
    check(Scene::wmoGroup(codes[i]) == groups[i], "WMO grouping");

  TFT_eSPI panel;
  panelTarget = &panel;
  Scene::setOverlayVisibility(false, false);
  Scene::invalidateCache();
  frames(panel, Scene::GR_DRIZZLE, 12.0f, 600);
  compareFull(panel, Scene::GR_DRIZZLE, 12.0f, 0.5f,
              "one minute drizzle leaves no old pixels");
  frames(panel, Scene::GR_HEAVYRAIN, 18.5f, 120);
  compareFull(panel, Scene::GR_HEAVYRAIN, 18.5f, 0.5f,
              "heavy rain and dusk terrain match full redraw");
  frames(panel, Scene::GR_PARTLY, 19.0f, 300);
  compareFull(panel, Scene::GR_PARTLY, 19.0f, 0.5f,
              "cloud wrap and precipitation exit match full redraw");
  frames(panel, Scene::GR_HEAVYSNOW, 5.5f, 300);
  compareFull(panel, Scene::GR_HEAVYSNOW, 5.5f, 0.5f,
              "snow respawn and dawn terrain match full redraw");
  frames(panel, Scene::GR_FOG, 7.5f, 30);
  compareFull(panel, Scene::GR_FOG, 7.5f, 0.5f,
              "fog transition matches full redraw");

  for (int group = 0; group < Scene::GR_COUNT; ++group) {
    for (float hour : {6.5f, 20.5f}) {
      frames(panel, (Scene::Group)group, hour, 30);
      compareFull(panel, (Scene::Group)group, hour, 0.5f,
                  "weather group day/night matches full redraw");
    }
  }

  for (int i = 0; i < 180; ++i)
    frames(panel, Scene::GR_CLEAR, 17.0f + i / 360.0f, 1);
  compareFull(panel, Scene::GR_CLEAR, 17.0f + 179 / 360.0f, 0.5f,
              "terrain follows gradual evening color changes");
  const int beforeStaticDraw = stripTransfers;
  Scene::draw(panel, 17.0f + 179 / 360.0f, Scene::GR_CLEAR,
              0.0f, 0.5f, true);
  check(stripTransfers == beforeStaticDraw,
        "frozen animation with unchanged scene transfers no strips");

  Scene::setOverlayVisibility(true, true);
  panel.fillRect(6, 222, 168, 18, 0x1234);
  panel.fillRect(0, 196, 320, 24, 0x4321);
  frames(panel, Scene::GR_RAIN, 12.0f, 20);
  check(panel.readPixel(10, 225) == 0x1234,
        "network badge survives scene transfer");
  check(panel.readPixel(90, 210) == 0x4321,
        "toast survives scene transfer");
  Scene::setOverlayVisibility(false, false);
  Scene::restoreGroundOverlay();
  frames(panel, Scene::GR_RAIN, 12.0f, 1, 0.0f);
  compareFull(panel, Scene::GR_RAIN, 12.0f, 0.5f,
              "badge and toast exit restore background and page dots");
  std::printf("scene pixels: %d failure(s)\n", failures);
  return failures ? 1 : 0;
}
