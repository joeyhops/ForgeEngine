#pragma once
#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <vector>

class btPairCachingGhostObject;
class btCollisionShape;
class btCollisionObject;

namespace forge {

class PhysicsWorld;

// Generic area-of-effect overlap detector
// Uses a btPairCachingGhostObject so enter/exit transitions are tracked
// without external bookkeeping
//
// two usages:
//    - Passive (fog gate?): provide onEnter/onExit callbacks - the volume
//    fires them automatically when overlap begins and ends
//
//    - Interaction-based (bonfire, pickup): construct with no callbacks and poll
//    isOverlapping() each frame. ForgeGame combines this with input checks:
//      if (vol->isOverlapping() && isKeyPressed(GLFW_KEY_B)) { /* etc. */ }
//
// call vol->update() once per frame, after physics.step(), in both modes.

class TriggerVolume {
public:
  // Sphere trigger
  TriggerVolume(PhysicsWorld& physics,
                const glm::vec3& center,
                float radius,
                std::function<void()> onEnter = {},
                std::function<void()> onExit = {});
  
  //Box trigger
  // halfExtents are the box half-dimensions in each axis
  TriggerVolume(PhysicsWorld& physics,
                const glm::vec3& center,
                const glm::vec3& halfExtents,
                std::function<void()> onEnter = {},
                std::function<void()> onExit = {});

  // Brush Trigger
  // hullVertices are the raw collision positions from the GeometryGenerator
  TriggerVolume(PhysicsWorld& physics,
                const std::vector<glm::vec3>& hullVertices,
                std::function<void()> onEnter = {},
                std::function<void()> onExit = {});

  ~TriggerVolume();

  // Must be called every frame after physics.step()
  // Comapres current overlapping set against previous frames set
  // and fires callbacks as transitions are detected
  // Also update isOverlapping() for polling
  void update();

  void setEnabled(bool enabled) { m_enabled = enabled; }
  bool isEnabled() const { return m_enabled; }

  bool isOverlapping() const { return m_isOverlapping; }

  const glm::vec3& getCenter() const { return m_center; }
private:
  void init(PhysicsWorld& physics, const glm::vec3& center, btCollisionShape* shape);

  PhysicsWorld& m_physics;
  btPairCachingGhostObject* m_ghost = nullptr;
  btCollisionShape* m_shape = nullptr;

  std::function<void()> m_onEnter;
  std::function<void()> m_onExit;

  glm::vec3 m_center{};
  bool m_enabled = true;
  bool m_isOverlapping = false;
};
}
