#pragma once

#include <string.h>
#include "utf8_copy.h"

namespace MediaText {

/**
 * Update the BLE text mailbox in receive order, while its caller holds the lock.
 * Metadata stays in the mailbox after flags are drained, so the BLE task can
 * identify a new track without reading the UI's state on another task.
 */
template <typename Mailbox>
inline void queueMetadata(Mailbox& mailbox, const char* title,
                          const char* artist) {
  char nextTitle[sizeof(mailbox.title)];
  char nextArtist[sizeof(mailbox.artist)];
  Utf8::copy(nextTitle, sizeof(nextTitle), title);
  Utf8::copy(nextArtist, sizeof(nextArtist), artist);
  const bool changed = strcmp(mailbox.title, nextTitle) != 0 ||
                       strcmp(mailbox.artist, nextArtist) != 0;
  Utf8::copy(mailbox.title, sizeof(mailbox.title), nextTitle);
  Utf8::copy(mailbox.artist, sizeof(mailbox.artist), nextArtist);
  mailbox.metaChanged = true;
  if (changed) {
    // An LRC already queued belongs to the previous metadata. Clear it now;
    // an LRC received after this META will replace the empty value normally.
    mailbox.lyric[0] = 0;
    mailbox.lyricChanged = true;
  }
}

}  // namespace MediaText
