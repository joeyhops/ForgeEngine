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
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
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

  loadConfig();
  if (!m_startupCharPath.empty() && !m_startupClipPath.empty())
    loadClipAndSkeleton(m_startupCharPath, m_startupClipPath);
  else if (!m_startupCharPath.empty())
    loadModel(m_startupCharPath);
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
    renderWeapon();

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
  resolveWeaponBone();
  m_charPath = charPath;

  if (!m_skinnedModel.material.albedo)
    m_skinnedModel.material.albedoTint = { 0.72f, 0.72f, 0.72f };

  m_animator.setSkeleton(m_skeleton);
  m_animator.setOwnerName("tae_editor");

  LOG_INFO("[TaeEditor] Loaded model: {} ({} bones)",
           charPath, m_skeleton.size()); 
  saveConfig();
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
 
  m_animator.setPreviewClip(m_clip, false);
 
  m_editedSync = m_clip->syncTrack.events;
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
 
  m_animator.setPreviewClip(clip, false);

  m_editedSync = m_clip->syncTrack.events;
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
 
static std::string toRelativePath(const std::string& path) {
  const std::string& root = forge::AssetManager::getAssetRoot();
  if (!root.empty() && path.rfind(root, 0) == 0)
    return path.substr(root.size());
  return path;
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
    j["clips"][entry.actionKey] = toRelativePath(entry.clipPath);
 
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
// Weapon preview
// ---------------------------------------------------------------------------

void TaeEditorApp::loadWeapon(const std::string& weaponId) {
  const forge::WeaponDef* def = forge::AssetManager::getWeaponDef(weaponId);
  if (!def) {
    LOG_ERROR("[TaeEditor] loadWeapon: '{}' not found in loaded defs", weaponId);
    return;
  }

  m_weaponDef = def;
  m_weaponModel = forge::ModelData{};

  if (!def->meshPath.empty())
    m_weaponModel = forge::AssetManager::loadModel(def->meshPath);
  else
    LOG_WARN("[TaeEditor] Weapon '{}' has no meshPath - transform will be visible via skeleton only",
             weaponId);

  decomposeOffset(def->meshOffset);
  resolveWeaponBone();
  LOG_INFO("[TaeEditor] Weapon preview: '{}' (bone '{}' -> idx {})",
           weaponId, def->boneAttach, m_weaponBoneIndex);

  saveConfig();
}

void TaeEditorApp::clearWeapon() {
  m_weaponDef = nullptr;
  m_weaponModel = forge::ModelData{};
  m_offsetEdit = WeaponOffsetEdit{};
  m_weaponBoneIndex = -1;
  saveConfig();
}

void TaeEditorApp::resolveWeaponBone() {
  m_weaponBoneIndex = -1;
  if (!m_weaponDef || m_skeleton.empty()) return;

  for (int i = 0; i < static_cast<int>(m_skeleton.size()); ++i) {
    if (m_skeleton[i].name == m_weaponDef->boneAttach) {
      m_weaponBoneIndex = i;
      return;
    }
  }

  LOG_WARN("[TaeEditor] Weapon bone '{}' not found in skeleton ({} bones) - weapon will not render",
           m_weaponDef->boneAttach, m_skeleton.size());
}

glm::mat4 TaeEditorApp::buildOffsetMatrix() const {
  glm::mat4 T = glm::translate(glm::mat4(1.0f), m_offsetEdit.pos);
  glm::mat4 R = glm::mat4_cast(glm::quat(glm::radians(m_offsetEdit.euler)));
  glm::mat4 S = glm::scale(glm::mat4(1.0f), m_offsetEdit.scale);
  return T * R * S;
}

void TaeEditorApp::decomposeOffset(const glm::mat4& m) {
  glm::vec3 scale, translation, skew;
  glm::vec4 perspective;
  glm::quat rotation;

  if (!glm::decompose(m, scale, rotation, translation, skew, perspective)) {
    LOG_WARN("[TaeEditor] decomposeOffset: glm::decompose failed -- resetting to identity");
    m_offsetEdit = WeaponOffsetEdit{};
    return;
  }

  m_offsetEdit.pos = translation;
  m_offsetEdit.scale = scale;
  m_offsetEdit.euler = glm::degrees(glm::eulerAngles(rotation));
  m_offsetEdit.dirty = false;
}

void TaeEditorApp::resetWeaponOffset() {
  if (!m_weaponDef) return;
  decomposeOffset(m_weaponDef->meshOffset);
}

void TaeEditorApp::saveWeaponOffset() {
  if (!m_weaponDef) return;

  const std::string jsonPath = forge::AssetManager::resolvePath("data/weapons.json");

  nlohmann::json j;
  {
    std::ifstream in(jsonPath);
    if (!in.is_open()) {
      LOG_ERROR("[TaeEditor] saveWeaponOffset: cannot open: '{}'", jsonPath);
      return;
    }
    try { in >> j; }
    catch (const nlohmann::json::exception& e) {
      LOG_ERROR("[TaeEditor] saveWeaponOffset: JSON parse error: {}", e.what());
      return;
    }
  }

  const std::string targetId = m_weaponDef->id;
  bool found = false;
  for (auto& w : j.at("weapons")) {
    if (w.at("id").get<std::string>() != targetId) continue;

    w["meshOffset"]["pos"] = { m_offsetEdit.pos.x, 
                               m_offsetEdit.pos.y,
                               m_offsetEdit.pos.z };
    w["meshOffset"]["rot"] = { m_offsetEdit.euler.x, 
                               m_offsetEdit.euler.y,
                               m_offsetEdit.euler.z };
    w["meshOffset"]["scale"] = { m_offsetEdit.scale.x, 
                               m_offsetEdit.scale.y,
                               m_offsetEdit.scale.z };

    found = true;
    break;
  }

  if (!found) {
    LOG_ERROR("[TaeEditor] saveWeaponOffset: '{}' not found in weapons array", targetId);
    return;
  }

  {
    std::ofstream out(jsonPath);
    if (!out.is_open()) {
      LOG_ERROR("[TaeEditor] saveWeaponOffset: cannot write to '{}'", jsonPath);
      return;
    }
    out << j.dump(2);
  }

  forge::AssetManager::loadWeaponDefs("data/weapons.json");
  m_weaponDef = forge::AssetManager::getWeaponDef(targetId);

  m_offsetEdit.dirty = false;

  LOG_INFO("[TaeEditor] Saved meshOffset for '{}' -> weapons.json", targetId);
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
 
  m_clip->syncTrack.events = m_editedSync;
  std::sort(m_clip->syncTrack.events.begin(), m_clip->syncTrack.events.end(),
            [](const forge::SyncEvent& a, const forge::SyncEvent& b){ return a.percentage < b.percentage; });
  m_clip->syncTrack.rehash();

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
  GLFWwindow* win = getWindow();
  const bool uiCapturing = getDebugUI().isCapturingMouse();

  double cx, cy;
  glfwGetCursorPos(win, &cx, &cy);
  const float dx = static_cast<float>(cx - m_prevMouseX);
  const float dy = static_cast<float>(cy - m_prevMouseY);
  m_prevMouseX = cx;
  m_prevMouseY = cy;

  const float scroll = s_scrollDelta;
  s_scrollDelta = 0.0f;

  // Left or Middle button drives orbit
  const bool dragBtn =
    glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS ||
    glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;

  // Begin an orbit session only if the drag STARTS over the viewport (ImGui
  // not capturing). Once started, keep orbiting until release event if the
  // cursor drifts over a panel - so a viewport drag is never interrupted,
  // and a widget interaction (WantCaptureMouse true) never spuriously starts
  // one
  if (!m_orbiting && dragBtn && !uiCapturing)
    m_orbiting = true;
  if (!dragBtn)
    m_orbiting = false;

  if (m_orbiting)
    m_camera.handleMouse(dx, dy, 0.0f);

  // Zoom: over the viewport, or while already orbiting (so scrolling a panel
  // still scrolls the panel)
  if (std::abs(scroll) > 0.001f && (!uiCapturing || m_orbiting))
    m_camera.handleMouse(0.0f, 0.0f, scroll);
}
 
// ---------------------------------------------------------------------------
// Render helpers
// ---------------------------------------------------------------------------
 
void TaeEditorApp::renderSkinnedModel() {
  // The bone positions in global transforms are in cm-scale space; the model
  // matrix scales them down to meters for the renderer.
  const glm::mat4 modelMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(m_modelUnitScale));
  const auto& boneMatrices    = m_animator.getBoneMatrices();
  getRenderer().drawSkinnedMesh(m_skinnedModel, modelMatrix, boneMatrices);
}

void TaeEditorApp::renderWeapon() {
  if (!m_weaponDef || m_weaponBoneIndex < 0) return;
  if (!m_weaponModel.hasRenderData()) return;

  const auto& gt = m_animator.getGlobalTransforms();
  if (m_weaponBoneIndex >= static_cast<int>(gt.size())) return;

  glm::mat4 boneWorld = glm::scale(glm::mat4(1.0f), glm::vec3(m_modelUnitScale))
                        * gt[m_weaponBoneIndex];

  for (int c = 0; c < 3; ++c) {
    float len = glm::length(glm::vec3(boneWorld[c]));
    if (len > 1e-6f) boneWorld[c] /= len;
  }

  getRenderer().drawMesh(m_weaponModel, boneWorld * buildOffsetMatrix());
}
 
void TaeEditorApp::renderSkeleton() {
  const auto& gt   = m_animator.getGlobalTransforms();
  const float kScale          = m_modelUnitScale;
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
  const float kScale   = m_modelUnitScale;
 
  for (const auto& ev : m_editedEvents) {
    if (ev.type != forge::AnimEventType::SpawnHitbox) continue;
    if (m_scrubTime < ev.startTime || m_scrubTime >= ev.endTime) continue;
    if (ev.capsules.empty()) continue;
 
    int boneIdx = -1;
    for (size_t i = 0; i < m_skeleton.size(); i++) {
      if (m_skeleton[i].name == ev.payload) { boneIdx = (int)i; break; }
    }
    if (boneIdx < 0) continue;
 
    glm::mat4 boneWorld = glm::scale(glm::mat4(1.0f), glm::vec3(kScale)) * gt[boneIdx];
    for (int c = 0; c < 3; c++) {
      float len = glm::length(glm::vec3(boneWorld[c]));
      if (len > 1e-6f) boneWorld[c] /= len;
    }

    glm::mat4 weaponWorld = boneWorld;
    if (m_weaponDef && m_weaponBoneIndex == boneIdx)
      weaponWorld = boneWorld * buildOffsetMatrix();

    for (const auto& seg : ev.capsules) {
      glm::vec3 wp0 = glm::vec3(weaponWorld * glm::vec4(seg.localP0, 1.0f));
      glm::vec3 wp1 = glm::vec3(weaponWorld * glm::vec4(seg.localP1, 1.0f));
      forge::DebugDraw::capsulePQ(wp0, wp1, seg.radius, { 1.0f, 0.45f, 0.1f });
    }
  }
}

void TaeEditorApp::loadConfig() {
  std::ifstream f("tae_editor.json");
  if (!f.is_open()) return;
  try {
    nlohmann::json j;
    f >> j;
    if (m_startupCharPath.empty() && j.contains("charPath"))
      m_startupCharPath = j["charPath"].get<std::string>();
    if (j.contains("animDir"))
      m_animDir = j["animDir"].get<std::string>();
    if (j.contains("modelUnitScale"))
      m_modelUnitScale = j["modelUnitScale"].get<float>();
    if (j.contains("weaponId")) {
      std::string wid = j["weaponId"].get<std::string>();
      if (!wid.empty())
        loadWeapon(wid);
    }
  } catch (nlohmann::json::exception& e) {
    LOG_WARN("[TaeEditor] tae_editor.json parse failed - using defaults ({})", e.what());
  }
}

void TaeEditorApp::saveConfig() {
  nlohmann::json j;
  if (!m_charPath.empty()) j["charPath"] = m_charPath;
  if (!m_animDir.empty()) j["animDir"] = m_animDir;
  j["modelUnitScale"] = m_modelUnitScale;
  if (m_weaponDef) j["weaponId"] = m_weaponDef->id;
  if (std::ofstream f("tae_editor.json"); f.is_open())
    f << j.dump(2);
}

void TaeEditorApp::setStartupChar(const std::string& charPath) {
  m_startupCharPath = charPath;
  saveConfig();
}

void TaeEditorApp::setAnimDir(const std::string& dir) {
  m_animDir = dir;
  saveConfig();
}


