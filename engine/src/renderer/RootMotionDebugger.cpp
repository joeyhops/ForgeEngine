#include <forge/RootMotionDebugger.h>

#ifdef FORGE_DEBUG

#include <forge/DebugDraw.h>

#include <glm/geometric.hpp>
#include <glm/common.hpp>

namespace forge {

// Tunables (all in meters/world units)
static constexpr size_t k_trailCap = 120; // ~2 s of history at 60 FPS
static constexpr float k_trailLift = 0.02f; // raise the trail off the floor
static constexpr float k_arrowHeight = 1.0f; // anchor arrow near torso
static constexpr float k_arrowScale = 8.0f; // per-frame delta -> length multiplier
static constexpr float k_arrowMinLen = 0.15f; // keep short pushes visible
static constexpr float k_arrowMaxLen = 1.50f; // stop fast pushes becoming the old 1-second ray
static const glm::vec3 k_dimColor = { 0.35f, 0.35f, 0.38f }; // velocity/idle trail

static glm::vec3 ColorForType(const std::string& t) {
  if (t == "ClipNode") return { 0.2f, 0.9f, 0.3f }; // green
  if (t == "Blend1D") return { 0.2f, 0.6f, 1.0f }; // blue
  if (t == "Blend2D") return { 1.0f, 0.7f, 0.1f }; // amber
  if (t == "StateMachine") return { 1.0f, 0.2f, 0.6f }; // magenta
  return { 1.0f, 1.0f, 1.0f };
}

// A readable 3D arrow: a shaft plus a four-barb conical head. Drawn thicker
// by laying three near-parallel shafts, so it does not vanish into a single-pixel
// ray
static void drawArrow(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color) {
  glm::vec3 dir = to - from;
  const float len = glm::length(dir);
  if (len < 1e-5f) return;
  dir /= len;

  // build a frame perpendicular to dir
  const glm::vec3 up = (glm::abs(dir.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
  const glm::vec3 side = glm::normalize(glm::cross(dir, up));
  const glm::vec3 vup = glm::normalize(glm::cross(side, dir));

  // Shaft, tripled with a small perpendicular offset to fake width
  const float t = 0.012f;
  DebugDraw::line(from, to, color);
  DebugDraw::line(from + side * t, to + side * t, color);
  DebugDraw::line(from - side * t, to - side * t, color);

  // four-barb head
  const float headLen = glm::min(0.12f, len * 0.35f);
  const float headW = headLen * 0.5f;
  const glm::vec3 base = to - dir * headLen;
  DebugDraw::line(to, base + side * headW, color);
  DebugDraw::line(to, base - side * headW, color);
  DebugDraw::line(to, base + vup * headW, color);
  DebugDraw::line(to, base - vup * headW, color);
}

std::string RootMotionDebugger::DominantType() const {
  const Entry* best = nullptr;
  float bestMag = 0.0f;
  for (const auto& e : m_frame) {
    const float m = glm::length(e.localDelta);
    if (m > bestMag) {
      bestMag = m;
      best = &e;
    }
  }
  return best ? best->typeName : std::string("ClipNode");
}

void RootMotionDebugger::PushSample(const glm::vec3& worldPos,
                                    const glm::vec3& worldRootDelta,
                                    bool rmActive) {
  m_trail.push_back({ worldPos, worldRootDelta, rmActive, DominantType() });
  if (m_trail.size() > k_trailCap) m_trail.pop_front();
}

void RootMotionDebugger::Draw() const {
  // 1. World trail - true scale. Bright + node-colored where root motion drove
  // the frame; dim grey where the velocity system (or nothing) did
  const glm::vec3 lift(0.0f, k_trailLift, 0.0f);
  for (size_t i = 1; i < m_trail.size(); ++i) {
    const Sample& a = m_trail[i - 1];
    const Sample& b = m_trail[i];
    const glm::vec3 col = b.rmActive ? ColorForType(b.dominant) : k_dimColor;
    DebugDraw::line(a.worldPos + lift, b.worldPos + lift, col);
  }

  // 2. Current frame arrow - only when this frames motion was root-motion driven
  if (!m_trail.empty()) {
    const Sample& s = m_trail.back();
    const float mag = glm::length(s.worldRootDelta);
    if (s.rmActive && mag > 1e-5f) {
      const glm::vec3 origin = s.worldPos + glm::vec3(0.0f, k_arrowHeight, 0.0f);
      const glm::vec3 dir = s.worldRootDelta / mag;
      const float length = glm::clamp(mag * k_arrowScale, k_arrowMinLen, k_arrowMaxLen);
      drawArrow(origin, origin + dir * length, ColorForType(s.dominant));
    }
  }
}

}

#endif
