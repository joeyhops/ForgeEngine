#include <forge/SampledEventsBuffer.h>

namespace forge {

void SampledEventsBuffer::MarkTransitionSource(uint16_t begin, uint16_t end) {
  const uint16_t hi = end < m_events.size() ? end : CurrentSize();
  for (uint16_t i = begin; i < hi; ++i) {
    // Clear ONLY the FromActiveBranch bits; leave other bits intact
    auto raw = static_cast<uint8_t>(m_events[i].flags);
    raw &= ~static_cast<uint8_t>(SampledEvent::Flags::FromActiveBranch);
    m_events[i].flags = static_cast<SampledEvent::Flags>(raw);
  }
}

}
