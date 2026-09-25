"""Compile the production text layout against its real U8g2 fonts."""
from pathlib import Path
import subprocess
import tempfile
import export_preview_fonts as fonts

fonts.STUB += r"""
class TFT_eSprite : public TFT_eSPI {
 public:
  uint16_t readPixel(int x,int y) { return pixels[y*W+x]; }
  void drawPixel(int x,int y,uint16_t c) { pixels[y*W+x]=c; }
};
"""
text_source = (fonts.ROOT / "firmware/src/text.cpp").as_posix()
fonts.EXPORTER = r"""
#include <cstdio>
#include <cstring>
#include <string>
#include "U8g2_for_TFT_eSPI.cpp"
#include "u8g2_fonts.c"
""" + '#include "' + text_source + '"\n' + r"""
int main() {
  TFT_eSPI screen; Txt::init(screen);
  int failures=0;
  auto check=[&](bool good,const char* msg){if(!good){++failures;printf("FAIL %s\n",msg);}};
  const char* samples[]={u8"每一句歌词都应该完整显示，中文与 Music 123 混排也不能溢出。每一句歌词都应该完整显示，中文与 Music 123 混排也不能溢出。",
    "iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii",
    u8"A mixed 歌名 with wide WWW and narrow iii, trailing space ",u8"末尾也要保留"};
  for(auto sample:samples) {
    std::string joined; size_t off=0; int lines=0;
    while(sample[off]) {
      char line[256]; const size_t used=Txt::wrapLine(CN16,sample+off,280,line,sizeof(line));
      check(used>0,"wrap always progresses"); if(!used)break;
      check(Txt::width(CN16,line)<=280,"real font wrap stays inside lyric viewport");
      joined+=line;off+=used;++lines;
    }
    check(joined==sample,"pagination loses no source text");
    if(sample==samples[0])check(lines>2,"long lyric reaches later page");
    for(int limit=8;limit<=184;limit+=4){char line[256];Txt::clip(CN16,sample,limit,line,sizeof(line));
      check(Txt::width(CN16,line)<=limit,"clipped mixed title respects measured width");}
  }
  char line[256];
  check(Txt::wrapLine(CN16,u8"第一句\r\n第二句",280,line,sizeof(line))==11 && strcmp(line,u8"第一句")==0,"CRLF consumes once");
  check(Txt::wrapLine(CN16,"",280,line,sizeof(line))==0 && !line[0],"empty lyric clears buffer");
  const char truncated[]={char(0xf0),0};
  check(Txt::utf8Len(truncated)==1,"truncated UTF8 does not overrun");
  // The ring page displays a 148px crop of the shared sprite. Check the real
  // font renderer at every scroll position, including negative glyph origins
  // and the wrap back to the first character, rather than just measuring text.
  const char* scrolling[]={
    u8"岂合巴山雪夜 相望别离 这一句很长的歌词应当继续滚动显示",
    u8"中文 English WWW iii 123 混合长歌词一直显示到最后",
    "A long English lyric must keep moving past the left edge of the screen"};
  for (const char* sample:scrolling) {
    const int width=Txt::width(CN12,sample), span=width+44;
    check(width>148,"ring regression sample actually overflows");
    std::vector<uint16_t> first;
    bool moved=false;
    for(int offset=0;offset<=span;++offset) {
      screen.clear();
      Txt::marquee(16,148,CN12,1,sample,offset);
      const auto actual=screen.pixels;
      if(offset==0) first=actual;
      screen.clear();
      const int off=offset%span;
      Txt::draw(-off,16,CN12,1,sample);
      if(span-off<148) Txt::draw(span-off,16,CN12,1,sample);
      int visible=0;
      for(int y=0;y<24;++y) for(int x=0;x<148;++x) {
        const int p=y*TFT_eSPI::W+x;
        check(actual[p]==screen.pixels[p],"marquee matches clipped real glyphs");
        visible+=actual[p]!=0;
        if(actual[p]!=first[p]) moved=true;
      }
      check(visible>0,"long ring lyric never becomes a blank frame");
      if(offset==span) check(actual==first,"ring lyric loops back to its start");
    }
    check(moved,"long ring lyric actually scrolls");
  }
  printf("real-font text layout: %d failure(s)\n",failures);
  return failures?1:0;
}
"""
with tempfile.TemporaryDirectory(prefix="pika-text-test-") as temporary:
    build = Path(temporary)
    (build / "Arduino.h").write_text("#pragma once\n#include <cstdint>\n", encoding="utf-8")
    executable = fonts.compile_exporter(build)
    subprocess.run([str(executable)], check=True)
