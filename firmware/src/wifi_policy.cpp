#include "wifi_config.h"
#include <string.h>

namespace WifiPolicy {

int selectBestKnown(const WiFiAP* aps, int apCount,
                    const char* const* knownSsids, int knownCount,
                    uint8_t triedMask) {
  if (!aps || !knownSsids || apCount <= 0 || knownCount <= 0) return -1;

  int bestKnown = -1;
  int bestRssi = -1000;
  for (int ap = 0; ap < apCount; ++ap) {
    for (int known = 0; known < knownCount && known < 8; ++known) {
      if (!knownSsids[known] || (triedMask & (1U << known))) continue;
      if (strcmp(aps[ap].ssid, knownSsids[known]) != 0) continue;
      if (bestKnown < 0 || aps[ap].rssi > bestRssi) {
        bestKnown = known;
        bestRssi = aps[ap].rssi;
      }
    }
  }
  return bestKnown;
}

}  // namespace WifiPolicy
