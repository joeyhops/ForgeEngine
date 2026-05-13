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
#include <forge/LightEnvironment.h>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
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
#ifdef __WIN32__
  forge::AssetManager::setAssetRoot("../../../../assets/");
#else
  forge::AssetManager::setAssetRoot("assets/");
#endif

  initRenderer(forge::AssetManager::getAssetRoot());

  getRenderer().setClearColor(0.12f, 0.12f, 0.14f);
  // Studio lighting rig for animation preview.
  // The PBR shader defaults to ambientIntensity = 0.05, which renders nearly
  // black with no other light sources. Set up a 3-point rig so the character
  // is clearly visible from any camera angle.

  // Ambient — soft cool sky tone, strong enough to read the mesh in shadow
  getRenderer().getLights().setAmbient({ 0.55f, 0.65f, 0.80f }, 0.40f);

  // Key light — warm, upper-right-front, primary illumination
  forge::Light key;
  key.posOrDir  = { 2.5f, 3.5f, 2.5f };
  key.color     = { 1.00f, 0.92f, 0.80f };
  key.intensity = 6.0f;
  key.range     = 30.0f;
  getRenderer().getLights().addPointLight(key);

  // Fill light — cool, camera-left, softens shadows
  forge::Light fill;
  fill.posOrDir  = { -3.0f, 1.5f, 1.5f };
  fill.color     = { 0.55f, 0.70f, 1.00f };
  fill.intensity = 2.0f;
  fill.range     = 30.0f;
  getRenderer().getLights().addPointLight(fill);

  // Rim light — behind, separates character from background
  forge::Light rim;
  rim.posOrDir  = { 0.0f, 2.5f, -4.0f };
  rim.color     = { 0.80f, 0.85f, 1.00f };
  rim.intensity = 3.0f;
  rim.range     = 30.0f;
  getRenderer().getLights().addPointLight(rim);

  glfwSetScrollCallback(getWindow(), onScroll);

  loadPayloadValidationData();

  m_ui = std::make_unique<TaeEditorUI>(*this);

  if (!m_startupCharPath.empty() && !m_startupClipPath.empty())
    loadClipAndSkeleton(m_startupCharPath, m_startupClipPath);
}

void TaeEditorApp::onShutdown() {
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
  float aspect = (float)getWidth() / (float)getHeight();
  forge::Camera frameCam = m_camera.toForgeCamera(aspect);
  getRenderer().beginFrame(frameCam, getWidth(), getHeight());

  if (m_clip) {
    if (m_skinnedModel.valid()) {
      renderSkinnedModel();
    }

    if (!m_skinnedModel.valid() || m_showSkeletonOverlay) {
      renderSkeleton();
    }

    renderActiveHitboxes();

    glm::mat4 vp = m_camera.projMatrix(aspect) * m_camera.viewMatrix();
    forge::DebugDraw::flush(vp);
  }

  m_ui->draw();
}

void TaeEditorApp::setScrubTime(float t) {
  if (!m_clip) return;
  m_scrubTime = std::clamp(t, 0.0f, m_clip->duration);
  m_animator.scrubTo(m_scrubTime);
}

void TaeEditorApp::loadModel(const std::string& charPath) {
  forge::SkinnedModelData data = forge::AssetManager::loadSkinnedModel(charPath);
  if (!data.valid()) {
    LOG_ERROR("[TaeEditor] Failed to load model: {}", charPath);
    return;
  }
  m_skinnedModel = data;
  m_skeleton = data.skeleton;
  m_charPath = charPath;

  if (!m_skinnedModel.material.albedo)
    m_skinnedModel.material.albedoTint = { 0.72f, 0.72f, 0.72f };

  m_animator.setSkeleton(m_skeleton);
  m_animator.setOwnerName("tae_editor");

  LOG_INFO("[TaeEditor] Loaded model: {} ({} bones)",
           charPath, m_skeleton.size()); 
}

void TaeEditorApp::loadClipFromPath(const std::string& clipPath,
                                    const std::string& overrideKey)
{
  if (m_skeleton.empty()) {
    LOG_ERROR("[TaeEditor] loadClipFromPath: no model loaded — call loadModel() first");
    return;
  }
 
  fs::path clipFs(clipPath);
  std::string taeKey = overrideKey.empty() ? clipFs.stem().string() : overrideKey;
 
  auto clip = forge::AssetManager::loadAnimationClip(clipPath, "", taeKey);
  if (!clip) {
    LOG_ERROR("[TaeEditor] Failed to load clip: {}", clipPath);
    return;
  }
  LOG_INFO("[Retarget] === Bone vs Track comparison ===");
  LOG_INFO("[Retarget] GLB skeleton bones ({}):", m_skeleton.size());
  for (const auto& b : m_skeleton)
    LOG_INFO("[Retarget]   bone: '{}'", b.name);

  LOG_INFO("[Retarget] FBX clip tracks ({}):", clip->tracks.size());
  for (const auto& t : clip->tracks)
    LOG_INFO("[Retarget]   track: '{}'", t.boneName);

  int matched = 0, missing = 0;
  for (const auto& b : m_skeleton) {
    if (clip->trackIndex.count(b.name)) matched++;
    else                                  missing++;
  }
  LOG_INFO("[Retarget] {} bones matched a track, {} did not", matched, missing);
 
  // Clear any active moveset context — this is now a "raw" clip load
  m_activeMovesetId.clear();
  m_activeActionKey.clear();
 
  m_clip         = clip;
  m_clipPath     = clipPath;
  m_sidecarPath  = toSidecarPath(clipPath);
 
  auto clipNode = std::make_shared<forge::ClipNode>(m_clip, false);
  clipNode->setOwnerName("tae_editor");
  m_animator.setGraph(clipNode);
 
  m_editedEvents  = m_clip->events;
  m_selectedEvent = -1;
  m_scrubTime     = 0.0f;
  m_animator.scrubTo(0.0f);
 
  LOG_INFO("[TaeEditor] Loaded clip: {} ({:.2f}s, {} events)",
           m_clip->name, m_clip->duration, m_editedEvents.size());
}

void TaeEditorApp::loadClipAndSkeleton(const std::string& charPath,
                                       const std::string& clipPath)
{
  // Legacy combined entry point — just delegates to the separated methods.
  // Model is only reloaded if the path has changed; this avoids redundant GPU uploads.
  if (m_charPath != charPath)
    loadModel(charPath);
  loadClipFromPath(clipPath);
}

void TaeEditorApp::loadMovesetAction(const std::string& charPath,
                                     const std::string& movesetId,
                                     const std::string& actionKey)
{
  const forge::MovesetDef* def = forge::AssetManager::getMovesetDef(movesetId);
  if (!def) {
    LOG_ERROR("[TaeEditor] loadMovesetAction: unknown moveset '{}'", movesetId);
    return;
  }
 
  auto clipIt = def->clips.find(actionKey);
  if (clipIt == def->clips.end()) {
    LOG_ERROR("[TaeEditor] loadMovesetAction: moveset '{}' has no action '{}'",
              movesetId, actionKey);
    return;
  }
 
  // Reload model only when it changes
  if (m_charPath != charPath)
    loadModel(charPath);
 
  const std::string& clipPath = clipIt->second;
  auto clip = forge::AssetManager::loadAnimationClip(clipPath, "", actionKey);
  if (!clip) {
    LOG_ERROR("[TaeEditor] Failed to load clip '{}' for action '{}'", clipPath, actionKey);
    return;
  }
 
  m_clip     = clip;
  m_clipPath = clipPath;
 
  // Sidecar named after the action key, not the file stem
  fs::path clipFs(clipPath);
  m_sidecarPath = (clipFs.parent_path() / "tae" / (actionKey + ".tae.json"))
                  .generic_string();
 
  m_activeMovesetId = movesetId;
  m_activeActionKey = actionKey;
 
  auto clipNode = std::make_shared<forge::ClipNode>(m_clip, false);
  clipNode->setOwnerName("tae_editor");
  m_animator.setGraph(clipNode);
 
  m_editedEvents  = m_clip->events;
  m_selectedEvent = -1;
  m_scrubTime     = 0.0f;
  m_animator.scrubTo(0.0f);
 
  LOG_INFO("[TaeEditor] Loaded moveset '{}' action '{}': {} ({:.2f}s, {} events)",
           movesetId, actionKey, m_clip->name, m_clip->duration,
           (int)m_editedEvents.size());
}

void TaeEditorApp::newMoveset(const std::string& id, const std::string& outputPath) {
  m_editableMovesetId         = id;
  m_editableMovesetOutputPath = outputPath;
  m_editableMoveset.clear();
  m_editableMovesetDirty      = false;
  LOG_INFO("[TaeEditor] New moveset: '{}'", id);
}
 
void TaeEditorApp::loadMovesetForEditing(const std::string& movesetId) {
  const forge::MovesetDef* def = forge::AssetManager::getMovesetDef(movesetId);
  if (!def) {
    LOG_ERROR("[TaeEditor] loadMovesetForEditing: unknown moveset '{}'", movesetId);
    return;
  }
 
  m_editableMovesetId         = movesetId;
  m_editableMovesetOutputPath = "";   // User must supply a save path explicitly
  m_editableMoveset.clear();
 
  for (const auto& [key, path] : def->clips)
    m_editableMoveset.push_back({ key, path });
 
  std::sort(m_editableMoveset.begin(), m_editableMoveset.end(),
            [](const MovesetEntry& a, const MovesetEntry& b) {
              return a.actionKey < b.actionKey;
            });
 
  m_editableMovesetDirty = false;
  LOG_INFO("[TaeEditor] Loaded moveset for editing: '{}' ({} actions)",
           movesetId, m_editableMoveset.size());
}
 
void TaeEditorApp::addEntryToMoveset(const std::string& actionKey,
                                     const std::string& clipPath)
{
  // Update existing entry if key already exists
  for (auto& entry : m_editableMoveset) {
    if (entry.actionKey == actionKey) {
      entry.clipPath         = clipPath;
      m_editableMovesetDirty = true;
      LOG_INFO("[TaeEditor] Updated moveset entry '{}' → {}", actionKey, clipPath);
      return;
    }
  }
  m_editableMoveset.push_back({ actionKey, clipPath });
  m_editableMovesetDirty = true;
  LOG_INFO("[TaeEditor] Added moveset entry '{}' → {}", actionKey, clipPath);
}
 
void TaeEditorApp::removeEntryFromMoveset(int idx) {
  if (idx < 0 || idx >= (int)m_editableMoveset.size()) return;
  m_editableMoveset.erase(m_editableMoveset.begin() + idx);
  m_editableMovesetDirty = true;
}
 
void TaeEditorApp::renameEntryKey(int idx, const std::string& newKey) {
  if (idx < 0 || idx >= (int)m_editableMoveset.size()) return;
  m_editableMoveset[idx].actionKey = newKey;
  m_editableMovesetDirty           = true;
}
 
void TaeEditorApp::saveMovesetToJson(const std::string& outputPath) {
  if (m_editableMovesetId.empty()) {
    LOG_ERROR("[TaeEditor] saveMovesetToJson: no moveset loaded for editing");
    return;
  }
 
  const std::string& savePath = outputPath.empty() ? m_editableMovesetOutputPath : outputPath;
  if (savePath.empty()) {
    LOG_ERROR("[TaeEditor] saveMovesetToJson: no output path set");
    return;
  }
 
  nlohmann::json j;
  j["id"]    = m_editableMovesetId;
  j["clips"] = nlohmann::json::object();
  for (const auto& entry : m_editableMoveset)
    j["clips"][entry.actionKey] = entry.clipPath;
 
  std::string absPath = forge::AssetManager::resolvePath(savePath);
  fs::create_directories(fs::path(absPath).parent_path());
 
  std::ofstream file(absPath);
  if (!file.is_open()) {
    LOG_ERROR("[TaeEditor] saveMovesetToJson: failed to open '{}' for writing", absPath);
    return;
  }
  file << j.dump(2);
 
  if (outputPath.empty() == false)
    m_editableMovesetOutputPath = outputPath;
 
  m_editableMovesetDirty = false;
  LOG_INFO("[TaeEditor] Saved moveset '{}' to {}", m_editableMovesetId, absPath);
}
 
// ---------------------------------------------------------------------------
// TAE sidecar
// ---------------------------------------------------------------------------
 
void TaeEditorApp::saveToSidecar() {
  if (!m_clip || m_sidecarPath.empty()) return;
 
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
  fs::path p(clipRelPath);
  fs::path sidecar = p.parent_path() / "tae" / (p.stem().string() + ".tae.json");
  return sidecar.generic_string();
}
 
// ---------------------------------------------------------------------------
// Payload validation
// ---------------------------------------------------------------------------
 
void TaeEditorApp::loadPayloadValidationData() {
  forge::AssetManager::loadWeaponDefs("data/weapons.json");
  forge::AssetManager::loadMovesetDefs("data/movesets.json");
 
  for (const auto& [id, def] : forge::AssetManager::getAllWeaponDefs())
    m_knownBones.push_back(def.boneAttach);
 
  for (const auto& [id, def] : forge::AssetManager::getAllMovesetDefs())
    for (const auto& [key, path] : def.clips)
      m_knownComboKeys.push_back(key);
 
  std::sort(m_knownBones.begin(), m_knownBones.end());
  m_knownBones.erase(std::unique(m_knownBones.begin(), m_knownBones.end()),
                     m_knownBones.end());
 
  std::sort(m_knownComboKeys.begin(), m_knownComboKeys.end());
  m_knownComboKeys.erase(std::unique(m_knownComboKeys.begin(), m_knownComboKeys.end()),
                         m_knownComboKeys.end());
}
 
// ---------------------------------------------------------------------------
// Orbit camera
// ---------------------------------------------------------------------------
 
forge::Camera TaeEditorApp::OrbitCamera::toForgeCamera(float aspect) const {
  forge::Camera cam(45.0f, aspect, 0.01f, 100.0f);
  float az = glm::radians(azimuth);
  float el = glm::radians(elevation);
  glm::vec3 eye = target + glm::vec3(
    radius * std::cos(el) * std::sin(az),
    radius * std::sin(el),
    radius * std::cos(el) * std::cos(az));
  cam.setPosition(eye);
  cam.setTarget(target);
  return cam;
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
  azimuth   = std::fmod(azimuth + dx * 0.4f, 360.0f);
  elevation = std::clamp(elevation - dy * 0.4f, -89.0f, 89.0f);
  radius    = std::clamp(radius - scroll * 0.15f, 0.3f, 20.0f);
}
 
void TaeEditorApp::handleCameraInput() {
  if (getDebugUI().isCapturingMouse()) return;
 
  GLFWwindow* win = getWindow();
  static double prevX = 0.0, prevY = 0.0;
  double cx, cy;
  glfwGetCursorPos(win, &cx, &cy);
  float dx = static_cast<float>(cx - prevX);
  float dy = static_cast<float>(cy - prevY);
  prevX = cx;
  prevY = cy;
 
  float scroll = s_scrollDelta;
  s_scrollDelta = 0.0f;
 
  if (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS)
    m_camera.handleMouse(dx, dy, scroll);
  else if (std::abs(scroll) > 0.001f)
    m_camera.handleMouse(0.0f, 0.0f, scroll);
}
 
// ---------------------------------------------------------------------------
// Render helpers
// ---------------------------------------------------------------------------
 
void TaeEditorApp::renderSkinnedModel() {
  // The bone positions in global transforms are in cm-scale space; the model
  // matrix scales them down to meters for the renderer.
  const glm::mat4 modelMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));
  const auto& boneMatrices    = m_animator.getBoneMatrices();
  getRenderer().drawSkinnedMesh(m_skinnedModel, modelMatrix, boneMatrices);
}
 
void TaeEditorApp::renderSkeleton() {
  const auto& gt   = m_animator.getGlobalTransforms();
  const float kScale          = 0.01f;
  const glm::vec3 boneColor   = { 0.3f, 0.9f, 0.4f };
  const glm::vec3 jointColor  = { 1.0f, 0.8f, 0.2f };
 
  for (size_t i = 0; i < m_skeleton.size(); i++) {
    glm::vec3 childPos = glm::vec3(gt[i][3]) * kScale;
    int parentIdx      = m_skeleton[i].parentIndex;
    if (parentIdx >= 0) {
      glm::vec3 parentPos = glm::vec3(gt[parentIdx][3]) * kScale;
      forge::DebugDraw::line(parentPos, childPos, boneColor);
    }
    forge::DebugDraw::sphere(childPos, 0.012f, jointColor);
  }
}
 
void TaeEditorApp::renderActiveHitboxes() {
  const auto& gt       = m_animator.getGlobalTransforms();
  const float kScale   = 0.01f;
 
  for (const auto& ev : m_editedEvents) {
    if (ev.type != forge::AnimEventType::SpawnHitbox) continue;
    if (m_scrubTime < ev.startTime || m_scrubTime >= ev.endTime) continue;
 
    int boneIdx = -1;
    for (size_t i = 0; i < m_skeleton.size(); i++) {
      if (m_skeleton[i].name == ev.payload) { boneIdx = (int)i; break; }
    }
    if (boneIdx < 0) continue;
 
    glm::mat4 boneWorld = glm::scale(glm::mat4(1.0f), glm::vec3(kScale)) * gt[boneIdx];
 
    // Strip scale from the basis vectors so the OBB box stays unit-sized
    for (int c = 0; c < 3; c++) {
      float len = glm::length(glm::vec3(boneWorld[c]));
      if (len > 1e-6f) boneWorld[c] /= len;
    }
    forge::DebugDraw::boxOBB(boneWorld, ev.hitboxExtents, { 1.0f, 0.45f, 0.1f });
  }
}
