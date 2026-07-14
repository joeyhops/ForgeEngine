#pragma once

#include <forge/AnimationClip.h>
#include <forge/HitboxTypes.h>

#include <cstdint>
#include <string>
#include <vector>

namespace forge {

// One event contributing to the pose THIS frame. Created by ClipNode/StateMachineNode
// during pass 1 traversal. Blend nodes weight scale as recursion unwinds, read once
// per frame by gameplay
struct SampledEvent {
  enum class Source : uint8_t { Clip, StateMachine };

  enum class Flags : uint8_t {
    None = 0,
    FromActiveBranch = 1 << 0, // false == this event is fading out of the transition
    SampledInReverse = 1 << 1, // reserved: clip playing backwards
  };

  AnimEventType type;
  float startPct = 0.0f;

  float weight = 1.0f; // blend weight at read time
  std::string payload;
  std::vector<CapsuleSegment> capsules;
  Source source = Source::Clip;
  Flags flags = Flags::FromActiveBranch;
  int16_t sourceNodeIdx = -1; // index into GraphDefinition node array
};

// Bitwise helpers for Flags
inline SampledEvent::Flags operator|(SampledEvent::Flags a, SampledEvent::Flags b) {
  return static_cast<SampledEvent::Flags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline bool HasFlag(SampledEvent::Flags v, SampledEvent::Flags f) {
  return (static_cast<uint8_t>(v) & static_cast<uint8_t>(f)) != 0;
}

class SampledEventsBuffer {
public:
  void Clear() { m_events.clear(); }

  // appends one event, returning idx so caller can record range
  uint16_t Append(const SampledEvent& e) {
    m_events.push_back(e);
    return static_cast<uint16_t>(m_events.size() - 1);
  }

  uint16_t Append(SampledEvent&& e) {
    m_events.push_back(std::move(e));
    return static_cast<uint16_t>(m_events.size() - 1);
  }

  // Multiply the weight of every event in [begin,end) by `scale`.
  // called by blend nodes as Pass 1 recursion unwinds
  void ScaleWeights(uint16_t begin, uint16_t end, float scale) {
    const uint16_t hi = end < m_events.size() ? end : static_cast<uint16_t>(m_events.size());

    for (uint16_t i = begin; i < hi; ++i) m_events[i].weight *= scale;
  }

  // Clears FromActiveBranch on [begin, end). Used by StateMachineNode to mark
  // the previous (fading-out) states events as transition-source.
  void MarkTransitionSource(uint16_t begin, uint16_t end);

  uint16_t CurrentSize() const { return static_cast<uint16_t>(m_events.size()); }
  const std::vector<SampledEvent>& Events() const { return m_events; }

  template <typename Pred>
  class FilteredView {
  public:
    FilteredView(const std::vector<SampledEvent>& v, Pred p)
      : m_v(v), m_pred(std::move(p)) {}
    class iterator {
    public:
      iterator(const std::vector<SampledEvent>* v, size_t i, const Pred* p)
        : m_v(v), m_i(i), m_pred(p) { advance(); }
      const SampledEvent& operator*() const { return (*m_v)[m_i]; }
      iterator& operator++() { ++m_i; advance(); return *this; }
      bool operator!=(const iterator& o) const { return m_i != o.m_i; }
    private:
      const std::vector<SampledEvent>* m_v;
      size_t m_i;
      const Pred* m_pred;

      void advance() { while (m_i < m_v->size() && !(*m_pred)((*m_v)[m_i])) ++m_i; }
    };
    iterator begin() const { return iterator(&m_v, 0, &m_pred); }
    iterator end() const { return iterator(&m_v, m_v.size(), &m_pred); }
  private:
    const std::vector<SampledEvent>& m_v;
    Pred m_pred;
  };

  template <typename Pred>
  FilteredView<Pred> FilteredRange(Pred&& pred) const {
    return FilteredView<Pred>(m_events, std::forward<Pred>(pred));
  }

private:
  std::vector<SampledEvent> m_events;
};

}
