#include <forge/SyncTrack.h>

#include <algorithm>
#include <cmath>

namespace forge {

void SyncTrack::rehash() {
  for (auto& e : events) e.id = HashName(e.name);
}

float SyncTrack::PercentageToSyncPos(float pct) const {
  const int n = count();
  if (n == 0) return pct;
  if (n == 1) return pct;

  pct = std::clamp(pct, 0.0f, 1.0f);

  if (pct < events[0].percentage) {
    const float p0 = events[n - 1].percentage;
    const float p1 = events[0].percentage + 1.0f;
    const float span = p1 - p0;
    const float frac = span > 1e-6f ? ((pct + 1.0f) - p0) / span : 0.0f;
    return static_cast<float>(n - 1) + std::clamp(frac, 0.0f, 1.0f);
  }
  // find the last event whose percentage is <= pct
  int seg = 0;
  for (int i = 0; i < n; ++i) {
    if (events[i].percentage <= pct) seg = i;
    else break;
  }

  const float p0 = events[seg].percentage;
  // next event, wrapping past the loop point for the final segment
  const bool wrap = (seg == n - 1);
  const float p1 = wrap ? events[0].percentage + 1.0f : events[seg + 1].percentage;

  const float span = (p1 - p0);
  const float frac = span > 1e-6f ? (pct - p0) / span : 0.0f;
  return static_cast<float>(seg) + std::clamp(frac, 0.0f, 1.0f);
}

float SyncTrack::SyncPosToPercentage(float syncPos) const {
  const int n = count();
  if (n <= 1) return syncPos;

  // decompose into segment index (mod n) and fraction through it
  float segF = std::floor(syncPos);
  float frac = syncPos - segF;
  int seg = static_cast<int>(segF) % n;
  if (seg < 0) seg += n;

  const float p0 = events[seg].percentage;
  const bool wrap = (seg == n - 1);
  const float p1 = wrap ? events[0].percentage + 1.0f : events[seg + 1].percentage;

  float pct = p0 + frac * (p1 - p0);
  // fold wrap segment back into [0,1).
  if (pct >= 1.0f) pct -= 1.0f;
  return pct;
}

bool SyncTrack::CompatibleWith(const SyncTrack& other) const {
  if (events.size() != other.events.size()) return false;
  for (size_t i = 0; i < events.size(); ++i)
    if (events[i].id != other.events[i].id) return false;
  return true;
}

SyncTrack SyncTrack::Blend(const SyncTrack& a, const SyncTrack& b, float w) {
  if (!a.CompatibleWith(b)) return a; // caller checks CompatibleWith first as guard
  
  SyncTrack out;
  out.events.resize(a.events.size());
  for (size_t i = 0; i < a.events.size(); ++i) {
    out.events[i].name = a.events[i].name;
    out.events[i].id = a.events[i].id;
    out.events[i].percentage = a.events[i].percentage * (1.0f - w)
                              + b.events[i].percentage * w;
  }

  return out;
}

}
