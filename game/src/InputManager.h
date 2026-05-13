#pragma once
#include "forge/Application.h"
#include <glm/glm.hpp>

#include <unordered_map>

struct GLFWwindow;

enum class InputAction {
  // mvmt
  MoveForward, MoveBack, MoveLeft, MoveRight,
  Sprint,
  //combat
  Dodge,
  AttackLight, AttackHeavy, Guard,
  // targeting and lockon
  LockOn,
  Interact,
  BonfireInteract,
  //Debug
  ReloadScript,
  ToggleUIMouse
};

// InputManager
// Game side abstraction over all input devices. Owns:
//  - action to keycode binding table
//  - per frame edge detection state for actions
//  - camera relative world space movement direction
//  - char local mvmt vector
//  - move speed scalar
//  - per frame mouse delta in px
//  - "first mouse" flag that prevents large delta jumps when cursor mode changes
//  Update contract: ForgeGame calls update() exactly once per frame,
//  at the start of onUpdate() before any consumer reads from it
//
//  Future controller support: bind() gains a gamepad-button overload
//  update() polls glfwGetGamepadState() and merges results with keyboard
//  device neutral consumer-facing API and no shape change

class InputManager {
public:
  InputManager();
  ~InputManager() = default;

  // Bind action to GLFW keycode
  // Defaults established in constructor; calling overrides
  void bind(InputAction a, int glfwKey);

  // Poll all devices once. computer world/local move vectors
  // move speed and mouse look deltas
  //
  // cameraForward: horizontal-plane unit vector from ThirdPersonCamera, used
  // to project move keys into world space
  //
  // characterFacaing: currrent player forward in world space
  // used to project world move onto character-local axes.
  void update(GLFWwindow* window,
              const glm::vec3& cameraForward,
              const glm::vec3& characterFacing);

  // edge/level queries on action state
  bool isPressed(InputAction a) const; // rising edge, this frame only
  bool isHeld(InputAction a) const; // currently down
  bool isReleased(InputAction a) const; // falling edge, this frame only

  const glm::vec3& getWorldMoveDir() const { return m_worldMoveDir; }
  const glm::vec2& getLocalMoveVector() const { return m_localMoveVector; }
  float getMoveSpeed() const { return m_moveSpeed; }

  const glm::vec2& getLookDelta() const { return m_lookDelta; }

  void resetLookTracking();

private:
  std::unordered_map<InputAction, int> m_bindings;
  std::unordered_map<InputAction, bool> m_prevState;
  std::unordered_map<InputAction, bool> m_currState;

  glm::vec3 m_worldMoveDir = { 0.0f, 0.0f, 0.0f };
  glm::vec2 m_localMoveVector = { 0.0f, 0.0f };
  float m_moveSpeed = 0.0f;

  // Mouse look state
  glm::vec2 m_lookDelta { 0.0f, 0.0f };
  double m_prevMouseX = 0.0;
  double m_prevMouseY = 0.0;
  bool m_firstMouse = true;

};
