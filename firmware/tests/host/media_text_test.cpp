#include <stdio.h>
#include <string.h>
#include "media_text.h"

struct Mailbox {
  char title[128] = {}, artist[128] = {}, lyric[256] = {};
  bool metaChanged = false, lyricChanged = false;
};

static int failures;
static void check(bool pass, const char* scenario) {
  if (!pass) {
    printf("FAIL: %s\n", scenario);
    ++failures;
  }
}
static void lyric(Mailbox& mailbox, const char* line) {
  Utf8::copy(mailbox.lyric, sizeof(mailbox.lyric), line);
  mailbox.lyricChanged = true;
}
static void drainFlags(Mailbox& mailbox) {
  mailbox.metaChanged = mailbox.lyricChanged = false;
}

int main() {
  Mailbox mailbox;
  MediaText::queueMetadata(mailbox, "晴天", "周杰伦");
  lyric(mailbox, "故事的小黄花");
  drainFlags(mailbox);

  MediaText::queueMetadata(mailbox, "晴天", "周杰伦");
  check(strcmp(mailbox.lyric, "故事的小黄花") == 0 && !mailbox.lyricChanged,
        "repeated metadata keeps the displayed lyric");
  lyric(mailbox, "从出生那年就飘着");
  MediaText::queueMetadata(mailbox, "晴天", "周杰伦");
  check(strcmp(mailbox.lyric, "从出生那年就飘着") == 0 && mailbox.lyricChanged,
        "repeated metadata keeps a pending lyric");

  MediaText::queueMetadata(mailbox, "七里香", "周杰伦");
  check(mailbox.metaChanged && mailbox.lyricChanged && mailbox.lyric[0] == 0,
        "old LRC followed by new META clears the queued old track lyric");
  lyric(mailbox, "窗外的麻雀");
  check(strcmp(mailbox.lyric, "窗外的麻雀") == 0 && mailbox.lyricChanged,
        "new META followed by new LRC retains the new track lyric");
  drainFlags(mailbox);

  MediaText::queueMetadata(mailbox, "七里香", "另一位歌手");
  check(mailbox.lyricChanged && mailbox.lyric[0] == 0,
        "artist-only track changes invalidate the old lyric");
  lyric(mailbox, "新版本歌词");
  drainFlags(mailbox);
  MediaText::queueMetadata(mailbox, "七里香", "另一位歌手");
  check(!mailbox.lyricChanged && strcmp(mailbox.lyric, "新版本歌词") == 0,
        "metadata comparison survives separate mailbox drains");

  // Comparisons use the same stored UTF-8 boundary as the UI: repeating a
  // longer-than-capacity title must not clear lyrics on every heartbeat.
  char longTitle[260];
  for (int i = 0; i < 86; ++i) memcpy(longTitle + i * 3, "歌", 3);
  longTitle[258] = 0;
  MediaText::queueMetadata(mailbox, longTitle, "歌手");
  check(strlen(mailbox.title) == 126, "metadata is copied at a UTF-8 boundary");
  lyric(mailbox, "长标题当前歌词");
  drainFlags(mailbox);
  MediaText::queueMetadata(mailbox, longTitle, "歌手");
  check(!mailbox.lyricChanged && strcmp(mailbox.lyric, "长标题当前歌词") == 0,
        "repeating a truncated title does not repeatedly erase lyrics");

  // Disconnect resets the whole mailbox in main.cpp; the next track must not
  // inherit the prior connection's metadata identity or text.
  mailbox = Mailbox{};
  MediaText::queueMetadata(mailbox, "晴天", "周杰伦");
  check(mailbox.metaChanged && mailbox.lyricChanged && mailbox.lyric[0] == 0,
        "a reconnected mailbox starts with empty lyrics");
  printf("media text mailbox: %d failure(s)\n", failures);
  return failures ? 1 : 0;
}
