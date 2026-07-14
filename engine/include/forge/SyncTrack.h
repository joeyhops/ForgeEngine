#pragma once

#include <forge/TypeSystem.h>

#include <cstdint>
#include <vector>
#include <string>

namespace forge {

// StringID == FNV-1a hash of an (authored) name. Sync-event comparisons on the hotpath
// (Does clip A's event 1 match clip B's event 1) compare IDs not strings
using StringID = uint32_t;

inline StringID HashName(const std::string& s) { return fnv1a(s); }

// One named marker at normalized pos in a clip
struct SyncEvent {
  float percentage = 0.0f;
  std::string name; // authored name, e.g. "LeftFootDown"
  StringID id = 0;
};

// per-clip percentage-sorted table of sync events. Empty means clip is not
// sync authored, falling back to dt advance
struct SyncTrack {
  std::vector<SyncEvent> events;

  bool empty() const { return events.empty(); }
  int count() const { return static_cast<int>(events.size()); }

  // Recompute cached ids after events/names are mutated
  void rehash();

  float PercentageToSyncPos(float pct) const;
  float SyncPosToPercentage(float syncPos) const;

  // Component-wise lerp of two tracks that share event count + order.
  // If counts differ, returns a copy of 'a' and leaves the caller to sort things
  // out (CompatibleWith)
  static SyncTrack Blend(const SyncTrack& a, const SyncTrack& b, float w);

  // true if both tracks have the same event count and same ids in order
  bool CompatibleWith(const SyncTrack& other) const;
};

// the unit of sync advance. Parents compute how far through sync space they moved
// on a given frame and hands the same range to each child, which resolves it into
// the childs own time.
//
// startSyncPos may exceed endSyncPos across a loop wrap; nodes handle the wrap
// explicitly
struct SyncTrackTimeRange {
  float startSyncPos = 0.0f;
  float endSyncPos = 0.0f;
  bool valid = false; // false == no sync this frame, use dt to advance
};

}
