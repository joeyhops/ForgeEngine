#include "TaeEditorApp.h"
#include "TaeEditorUI.h"

#include <forge/AssetManager.h>
#include <forge/WeaponDef.h>
#include <forge/MovesetDef.h>
#include <forge/DebugUI.h>
#include <forge/DebugDraw.h>
#include <forge/Logger.h>
#include <forge/AnimGraph.h>
#include <forge/Animator.h>
#include <forge/TAESerialization.h>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

// Static scroll accumulator - glfwSetScrollCallback requires
// a free function
static float s_scrollDelta = 0.0f;
static void onScroll(GLFWwindow*, double, double yOff) {
  s_scrollDelta += static_cast<float>(yOff);
}

TaeEditorApp::TaeEditorApp(int w, int h, const char* title)
  : forge::Application(w, h, title) {}

TaeEditorApp::~TaeEditorApp() = default;

void TaeEditorApp::onInit() {
  setClearColor(0.12f, 0.12f, 0.14f);

#ifdef __WIN32__
  forge::AssetManager::setAssetRoot("../../../../assets/");
#else
  forge::AssetManager::setAssetRoot("assets/");
#endif

#ifdef __APPLE__
  m_lineShader = forge::AssetManager::loadShader(
    "shaders/mac/debug_line.vert",
    "shaders/mac/debug_line.frag"
  );
#else
  m_lineShader = forge::AssetManager::loadShader(
    "shaders/win/debug_line.vert",
    "shaders/win/debug_line.frag"
  );
#endif
  // Line Shader for DebugDraw
  forge::DebugDraw::init(m_lineShader.get());

  glfwSetScrollCallback(getWindow(), onScroll);

  loadPayloadValidationData();

  m_ui = std::make_unique<TaeEditorUI>(*this);

  if (!m_startupCharPath.empty() && !m_startupClipPath.empty())
    loadClipAndSkeleton(m_startupCharPath, m_startupClipPath);
}

void TaeEditorApp::onShutdown() {
  forge::DebugDraw::shutdown();
}

void TaeEditorApp::onUpdate(float dt) {
  handleCameraInput();

  if (m_playing && m_clip) {
    m_scrubTime += dt;
    if (m_scrubTime >= m_clip->duration) {
      m_scrubTime = 0.0f;
    }
    m_animator.scrubTo(m_scrubTime);
  }
}

void TaeEditorApp::onRender() {
  if (m_clip) {
    renderSkeleton();
    renderActiveHitboxes();

    glm::mat4 vp = m_camera.projMatrix((float)getWidth() / getHeight())
                 * m_camera.viewMatrix();
    forge::DebugDraw::flush(vp);
  }

  m_ui->draw();
}

void TaeEditorApp::setScrubTime(float t) {
  if (!m_clip) return;
  m_scrubTime = std::clamp(t, 0.0f, m_clip->duration);
  m_animator.scrubTo(m_scrubTime);
}

void TaeEditorApp::loadClipAndSkeleton(const std::string& charPath,
                                       const std::string& clipPath)
{
  // entering raw mode
  m_activeMovesetId.clear();
  m_activeActionKey.clear();

  // Load skeleton from character model glb
  forge::SkinnedModelData charData = forge::AssetManager::loadSkinnedModel(charPath);
  if (!charData.valid()) {
    LOG_ERROR("[TaeEditor] Failed to load character: {}", charPath);
    return;
  }

  std::filesystem::path clipFs(clipPath);
  std::string taeKey = clipFs.stem().string();

  auto clip = forge::AssetManager::loadAnimationClip(clipPath, "", taeKey);
  if (!clip) {
    LOG_ERROR("[TaeEditor] Failed to load clip: {}", clipPath);
    return;
  }

  m_clip = clip;
  m_skeleton = charData.skeleton;
  m_charPath = charPath;
  m_clipPath = clipPath;
  m_sidecarPath = toSidecarPath(clipPath);

  // Build single clip-node graph
  auto clipNode = std::make_shared<forge::ClipNode>(m_clip, false);
  clipNode->setOwnerName("tae_editor");

  m_animator.setSkeleton(m_skeleton);
  m_animator.setOwnerName("tae_editor");
  m_animator.setGraph(clipNode);

  m_editedEvents = m_clip->events;
  m_selectedEvent = -1;
  m_scrubTime = 0.0f;
  m_animator.scrubTo(0.0f);

  LOG_INFO("[TaeEditor] Loaded: {} ({:.2f}s, {} events)",
           m_clip->name, m_clip->duration, m_editedEvents.size());
}

void TaeEditorApp::loadMovesetAction(const std::string& charPath,
                                     const std::string& movesetId,
                                     const std::string& actionKey)
{
  const forge::MovesetDef* def = forge::AssetManager::getMovesetDef(movesetId);
  if (!def) {
    LOG_ERROR("[TaeEditor] loadMovesetAction: unknown moveset: '{}'", movesetId);
    return;
  }

  auto clipIt = def->clips.find(actionKey);
  if (clipIt == def->clips.end()) {
    LOG_ERROR("[TaeEditor] loadMovesetAction: moveset '{}' has no action '{}'", movesetId, actionKey);
    return;
  }
  const std::string& clipPath = clipIt->second;

  forge::SkinnedModelData charData = forge::AssetManager::loadSkinnedModel(charPath);
  if (!charData.valid()) {
    LOG_ERROR("[TaeEditor] failed to load character: {}", charPath);
    return;
  }

  auto clip = forge::AssetManager::loadAnimationClip(clipPath, "", actionKey);
  if (!clip) {
    LOG_ERROR("[TaeEditor] Failed to load clip '{}' for action '{}'", clipPath, actionKey);
    return;
  }

  m_clip = clip;
  m_skeleton = charData.skeleton;
  m_charPath = charPath;
  m_clipPath = clipPath;

  // Sidecar path, named after action key NOT the animation file
  fs::path clipFs(clipPath);
  m_sidecarPath = (clipFs.parent_path() / "tae" / (actionKey + ".tae.json"))
                  .generic_string();

  m_activeMovesetId = movesetId;
  m_activeActionKey = actionKey;

  auto clipNode = std::make_shared<forge::ClipNode>(m_clip, false);
  clipNode->setOwnerName("tae_editor");

  m_animator.setSkeleton(m_skeleton);
  m_animator.setOwnerName("tae_editor");
  m_animator.setGraph(clipNode);

  m_editedEvents = m_clip->events;
  m_selectedEvent = -1;
  m_scrubTime = 0.0f;
  m_animator.scrubTo(0.0f);

  LOG_INFO("[TaeEditor] Loaded moveset '{}' action '{}': {} ({:.2f}s, {} events)",
           movesetId, actionKey, m_clip->name, m_clip->duration, (int)m_editedEvents.size());
}

void TaeEditorApp::loadPayloadValidationData() {
  forge::AssetManager::loadWeaponDefs("data/weapons.json");
  forge::AssetManager::loadMovesetDefs("data/movesets.json");

  for (const auto& [id, def] : forge::AssetManager::getAllWeaponDefs())
    m_knownBones.push_back(def.boneAttach);

  for (const auto& [id, def] : forge::AssetManager::getAllMovesetDefs())
    for (const auto& [key, path] : def.clips)
      m_knownComboKeys.push_back(key);

  std::sort(m_knownBones.begin(), m_knownBones.end());
  m_knownBones.erase(std::unique(m_knownBones.begin(), m_knownBones.end()), m_knownBones.end());

  std::sort(m_knownComboKeys.begin(), m_knownComboKeys.end());
  m_knownComboKeys.erase(std::unique(m_knownComboKeys.begin(), m_knownComboKeys.end()), m_knownComboKeys.end());
}

void TaeEditorApp::saveToSidecar() {
  if (!m_clip || m_sidecarPath.empty()) return;

  // Sort edited events list by time, write them back to the TAE, then save
  m_clip->events = m_editedEvents;
  std::sort(m_clip->events.begin(), m_clip->events.end(),
            [](const forge::AnimEvent& a, const forge::AnimEvent& b) {
              return a.startTime < b.startTime;
            });

  std::string absPath = forge::AssetManager::resolvePath(m_sidecarPath);
  forge::TAESerialization::save(absPath, *m_clip);
  LOG_INFO("[TaeEditor] Saved: {}", absPath);
}

std::string TaeEditorApp::toSidecarPath(const std::string& clipRelPath) const {
  // In: "../movesets/sword/r1.glb"
  // Out: "../movesets/sword/tae/r1.tae.json"
  fs::path p(clipRelPath);
  fs::path sidecar = p.parent_path() / "tae" / (p.stem().string() + ".tae.json");
  return sidecar.generic_string();
}

glm::mat4 TaeEditorApp::OrbitCamera::viewMatrix() const {
  float az = glm::radians(azimuth);
  float el = glm::radians(elevation);
  glm::vec3 eye = target + glm::vec3(
    radius * std::cos(el) * std::sin(az),
    radius * std::sin(el),
    radius * std::cos(el) * std::cos(az));
  return glm::lookAt(eye, target, { 0.0f, 1.0f, 0.0f });
}

glm::mat4 TaeEditorApp::OrbitCamera::projMatrix(float aspect) const {
  return glm::perspective(glm::radians(45.0f), aspect, 0.01f, 100.0f);
}

void TaeEditorApp::OrbitCamera::handleMouse(float dx, float dy, float scroll) {
  azimuth = std::fmod(azimuth + dx * 0.4f, 360.0f);
  elevation = std::clamp(elevation - dy * 0.4f, -89.0f, 89.0f);
  radius = std::clamp(radius - scroll * 0.15f, 0.3f, 20.0f);
}

void TaeEditorApp::handleCameraInput() {
  if (getDebugUI().isCapturingMouse()) return;

  GLFWwindow* win = getWindow();
  static double prevX = 0.0f, prevY = 0.0f;
  double cx, cy;
  glfwGetCursorPos(win, &cx, &cy);
  float dx = static_cast<float>(cx - prevX);
  float dy = static_cast<float>(cy - prevY);
  prevX = cx;
  prevY = cy;

  float scroll = s_scrollDelta;
  s_scrollDelta = 0.0f; // consume scroll

  if (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS)
    m_camera.handleMouse(dx, dy, scroll);
  else if (std::abs(scroll) > 0.001f)
    m_camera.handleMouse(0.0f, 0.0f, scroll);
}

void TaeEditorApp::renderSkeleton() {
  const auto& gt = m_animator.getGlobalTransforms();
  const float modelScale = 0.01f;
  const glm::vec3 boneColor = { 0.3f, 0.9f, 0.4f };
  const glm::vec3 jointColor = { 1.0f, 0.8f, 0.2f };

  for (size_t i = 0; i < m_skeleton.size(); i++) {
    glm::vec3 childPos = glm::vec3(gt[i][3]) * modelScale;
    int parentIdx = m_skeleton[i].parentIndex;
    if (parentIdx >= 0) {
      glm::vec3 parentPos = glm::vec3(gt[parentIdx][3]) * modelScale;
      forge::DebugDraw::line(parentPos, childPos, boneColor);
    }
    forge::DebugDraw::sphere(childPos, 0.012f, jointColor);
  }
}

void TaeEditorApp::renderActiveHitboxes() {
  const auto& gt = m_animator.getGlobalTransforms();
  const float modelScale = 0.01f;

  for (const auto& ev : m_editedEvents) {
    if (ev.type != forge::AnimEventType::SpawnHitbox) continue;
    if (m_scrubTime < ev.startTime || m_scrubTime >= ev.endTime) continue;

    int boneIdx = -1;
    for (size_t i = 0; i < m_skeleton.size(); i++) {
      if (m_skeleton[i].name == ev.payload) { boneIdx = (int)i; break; }
    }
    if (boneIdx < 0) continue;

    glm::mat4 boneWorld = glm::scale(glm::mat4(1.0f), glm::vec3(modelScale))
                        * gt[boneIdx];

    for (int c = 0; c < 3; c++) {
      float len = glm::length(glm::vec3(boneWorld[c]));
      if (len > 1e-6f) boneWorld[c] /= len;
    }
    forge::DebugDraw::boxOBB(boneWorld, ev.hitboxExtents, { 1.0f, 0.45f, 0.1f });
  }
}
