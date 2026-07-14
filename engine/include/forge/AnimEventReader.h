#pragma once
#include <forge/SampledEventsBuffer.h>

#include <unordered_set>
#include <cstdint>

namespace forge {

// Consumer-side adapter over the per-frame SampledEventsBuffer.
//
// Call refresh() once per frame with animators buffer, query, then
// the internal snapshot rolls forward.
class AnimEventReader {
public:
  // Ingest this frames buffer, weightThreshold ignores events blended below
  // `minweight` Default 0.5 matches the "dominant branch" rule
  void refresh(const SampledEventsBuffer& buff, float minWeight = 0.5f) {
    m_prev.swap(m_curr);
    m_curr.clear();
    for (const auto& e : buff.Events()) {
      if (e.weight < minWeight) continue;
      if (!HasFlag(e.flags, SampledEvent::Flags::FromActiveBranch)) continue; // ignore fading events

      m_curr.insert(key(e.type, e.payload));
    }
  }

  bool active(AnimEventType t, const std::string& p = {}) const { return m_curr.count(key(t, p)) != 0; }
  bool opened(AnimEventType t, const std::string& p = {}) const {
    return m_curr.count(key(t, p)) && !m_prev.count(key(t, p));
  }
  bool closed(AnimEventType t, const std::string& p = {}) const {
    return !m_curr.count(key(t, p)) && m_prev.count(key(t, p));
  }

  // For payload agnostic queries
  bool activeType(AnimEventType t) const { return anyOfType(m_curr, t); }
  bool openedType(AnimEventType t) const { return anyOfType(m_curr, t) && !anyOfType(m_prev, t); }
  bool closedType(AnimEventType t) const { return !anyOfType(m_curr, t) && anyOfType(m_prev, t); }

private:
  static uint64_t key(AnimEventType t, const std::string& p) {
    const uint32_t ph = static_cast<uint32_t>(std::hash<std::string>{}(p));
    return (static_cast<uint64_t>(t) << 32) | static_cast<uint64_t>(ph);
  }

  static bool anyOfType(const std::unordered_set<uint64_t>& s, AnimEventType t) {
    const uint64_t hi = static_cast<uint64_t>(t) << 32;
    for (uint64_t k : s) if ((k & 0xFFFFFFFF00000000ull) == hi) return true;
    return false;
  }
  std::unordered_set<uint64_t> m_curr, m_prev;
};

}
