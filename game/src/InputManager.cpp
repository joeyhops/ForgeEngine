#include "InputManager.h"
#include "forge/Application.h"

#include <GLFW/glfw3.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

InputManager::InputManager() {
  // Default bindings
  m_bindings[InputAction::MoveForward] = GLFW_KEY_W;
  m_bindings[InputAction::MoveBack] = GLFW_KEY_S;
  m_bindings[InputAction::MoveLeft] = GLFW_KEY_A;
  m_bindings[InputAction::MoveRight] = GLFW_KEY_D;
  m_bindings[InputAction::Sprint] = GLFW_KEY_LEFT_SHIFT;
  m_bindings[InputAction::Dodge] = GLFW_KEY_SPACE;
  m_bindings[InputAction::AttackLight] = GLFW_KEY_J;
  m_bindings[InputAction::AttackHeavy] = GLFW_KEY_K;
  m_bindings[InputAction::Guard] = GLFW_KEY_L;
  m_bindings[InputAction::LockOn] = GLFW_KEY_TAB;
  m_bindings[InputAction::Interact] = GLFW_KEY_E;
  m_bindings[InputAction::BonfireInteract] = GLFW_KEY_B;
  m_bindings[InputAction::ReloadScript] = GLFW_KEY_F9;
  m_bindings[InputAction::ToggleUIMouse] = GLFW_KEY_GRAVE_ACCENT;
}

void InputManager::bind(InputAction a, int glfwKey) {
  m_bindings[a] = glfwKey;
}

void InputManager::resetLookTracking() {
  m_firstMouse = true;
  m_lookDelta = glm::vec2(0.0f);
}

void InputManager::update(GLFWwindow* window,
                          const glm::vec3& cameraForward,
                          const glm::vec3& characterFacing)
{
  m_prevState = m_currState;
  for (const auto& [action, key] : m_bindings)
    m_currState[action] = (glfwGetKey(window, key) == GLFW_PRESS);

  double mx, my;
  glfwGetCursorPos(window, &mx, &my);
  if (m_firstMouse) {
    m_lookDelta = glm::vec2(0.0f);
    m_firstMouse = false;
  } else {
    m_lookDelta.x = static_cast<float>(mx - m_prevMouseX);
    m_lookDelta.y = static_cast<float>(my - m_prevMouseY);
  }
  m_prevMouseX = mx;
  m_prevMouseY = my;

  const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
  glm::vec3 camFwd = cameraForward;
  camFwd.y = 0.0f;
  if (glm::length2(camFwd) > 1e-6f) camFwd = glm::normalize(camFwd);
  glm::vec3 camRight = glm::normalize(glm::cross(camFwd, worldUp));

  glm::vec3 move(0.0f);
  if (isHeld(InputAction::MoveForward)) move += camFwd;
  if (isHeld(InputAction::MoveBack)) move -= camFwd;
  if (isHeld(InputAction::MoveLeft)) move -= camRight;
  if (isHeld(InputAction::MoveRight)) move += camRight;

  float moveLen = glm::length(move);
  m_worldMoveDir = (moveLen > 1e-4f) ? (move / moveLen) : glm::vec3(0.0f);

  glm::vec3 charFwd = characterFacing;
  charFwd.y = 0.0f;
  if (glm::length2(charFwd) > 1e-6f) charFwd = glm::normalize(charFwd);
  glm::vec3 charRight = glm::normalize(glm::cross(charFwd, worldUp));

  m_localMoveVector.x = glm::dot(m_worldMoveDir, charRight);
  m_localMoveVector.y = glm::dot(m_worldMoveDir, charFwd);

  if (moveLen < 1e-4f) m_moveSpeed = 0.0f;
  else if (isHeld(InputAction::Sprint)) m_moveSpeed = 2.0f;
  else m_moveSpeed = 1.0f;
}

bool InputManager::isPressed(InputAction a) const {
  auto c = m_currState.find(a);
  auto p = m_prevState.find(a);
  bool curr = (c != m_currState.end() && c->second);
  bool prev = (p != m_prevState.end() && p->second);
  return curr && !prev;
}

bool InputManager::isHeld(InputAction a) const {
  auto c = m_currState.find(a);
  return c != m_currState.end() && c->second;
}

bool InputManager::isReleased(InputAction a) const {
  auto c = m_currState.find(a);
  auto p = m_prevState.find(a);
  bool curr = (c != m_currState.end() && c->second);
  bool prev = (p != m_prevState.end() && p->second);
  return !curr && prev;
}

