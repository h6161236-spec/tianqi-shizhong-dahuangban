# Firmware bitmap fonts in the preview

`../fonts.js` contains the exact glyphs used by `firmware/src/text.cpp`.
It is generated with the project's vendored U8g2 decoder and font data; it does
not depend on an operating-system typeface, browser text rendering, or network.
Load it before the preview's drawing code.

```js
window.PIKA_FONTS.CN12
// { ascent: 8, descent: -2, glyphs: { "65": [...], ... } }
```

The four keys match the firmware's font enum: `CN12`, `CN16`, `NUM14`, `NUM22`.
Each Chinese face exports all 7,539 supported glyphs, including ASCII and the
GB2312 Chinese repertoire. Each numeric face contains 18 supported glyphs.

Each glyph is `[advance, xOffset, topOffset, width, height, ...rowMasks]`:

- Keys are decimal Unicode code points, not UTF-8 bytes.
- `advance` is the cursor movement after drawing the glyph.
- `xOffset` and `topOffset` are relative to the cursor's **baseline** position.
- Each following integer is one raster row; bit 0 is the leftmost pixel.
- Paint each set bit as a solid 1 × 1 pixel, without antialiasing.
- A missing glyph draws nothing and contributes no advance, like the firmware.

For example:

```js
function paintGlyph(ctx, face, codePoint, x, baseline) {
  const g = face.glyphs[codePoint];
  if (!g) return 0;
  for (let row = 0; row < g[4]; row++) {
    for (let col = 0; col < g[3]; col++) {
      if (g[5 + row] & (1 << col)) {
        ctx.fillRect(x + g[1] + col, baseline + g[2] + row, 1, 1);
      }
    }
  }
  return g[0];
}
```

Use integer positions and apply the same clipping rectangle as the firmware.
`Txt::drawTop(x, top, ...)` puts the baseline at `top + face.ascent`; `ascent`
is U8g2's `ascent_A`, **not** the total Chinese glyph height.

U8g2's measured text width differs from its cursor advance: sum all advances,
then, if the final glyph's bitmap width is nonzero, replace that glyph's advance
with `width + xOffset`. A trailing space keeps its normal advance. The vendored
decoder retains the previous glyph's width and offset after a missing glyph;
strict compatibility should retain these values while setting the missing
glyph's advance to zero.

## Rebuild and verify

From the project root:

```text
python firmware/tools/export_preview_fonts.py
python firmware/tools/export_preview_fonts.py --check
```

Python 3 and a C++ compiler are sufficient: MSVC Build Tools on Windows, or
`c++`, `g++`, or `clang++` elsewhere. The generator compiles the **unmodified**
vendored decoder into a temporary directory with a small in-memory display.
It verifies reconstructed bitmap composition, advances, and measured widths
against the decoder for Chinese lyrics, mixed ASCII/Chinese, descenders,
punctuation, spaces, and numeric strings. `--check` also verifies that the
committed JavaScript matches the current sources byte for byte.

Font and decoder copyright remains with their original authors; see
`firmware/lib/U8g2_for_TFT_eSPI/LICENSE` and the vendored source notices.
