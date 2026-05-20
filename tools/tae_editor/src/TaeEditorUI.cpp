#include "TaeEditorUI.h"
#include "TaeEditorApp.h"

#include <forge/AssetManager.h>
#include <forge/MovesetDef.h>
#include <forge/WeaponDef.h>
#include <forge/Logger.h>
#include <forge/DebugUI.h>

#include <imgui.h>

#include <filesystem>
#include <algorithm>
#include <numeric>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Event type metadata (unchanged from original)
// ---------------------------------------------------------------------------

static const forge::AnimEventType k_allEventTypes[] = {
  forge::AnimEventType::SpawnHitbox,
  forge::AnimEventType::IFrame,
  forge::AnimEventType::ComboWindow,
  forge::AnimEventType::ParryWindow,
  forge::AnimEventType::DeflectWindow,
  forge::AnimEventType::HyperArmorBegin,
  forge::AnimEventType::HyperArmorEnd,
  forge::AnimEventType::SoundOneShot,
  forge::AnimEventType::SFXSpawn,
  forge::AnimEventType::SpawnImpactVFX,
  forge::AnimEventType::ScriptedSequence,
  forge::AnimEventType::SetKinematic,
  forge::AnimEventType::RestorePhysics,
  forge::AnimEventType::RootMotionBegin,
  forge::AnimEventType::RootMotionEnd,
  forge::AnimEventType::RootMotionScale,
  forge::AnimEventType::StartTrail,
  forge::AnimEventType::StopTrail,
};
static const int k_eventTypeCount = (int)(sizeof(k_allEventTypes) / sizeof(k_allEventTypes[0]));

const char* TaeEditorUI::eventTypeName(forge::AnimEventType t) {
  switch (t) {
    case forge::AnimEventType::SpawnHitbox:      return "SpawnHitbox";
    case forge::AnimEventType::IFrame:           return "IFrame";
    case forge::AnimEventType::ComboWindow:      return "ComboWindow";
    case forge::AnimEventType::ParryWindow:      return "ParryWindow";
    case forge::AnimEventType::DeflectWindow:    return "DeflectWindow";
    case forge::AnimEventType::HyperArmorBegin:  return "HyperArmorBegin";
    case forge::AnimEventType::HyperArmorEnd:    return "HyperArmorEnd";
    case forge::AnimEventType::SoundOneShot:     return "SoundOneShot";
    case forge::AnimEventType::SFXSpawn:         return "SFXSpawn";
    case forge::AnimEventType::SpawnImpactVFX:   return "SpawnImpactVFX";
    case forge::AnimEventType::ScriptedSequence: return "ScriptedSequence";
    case forge::AnimEventType::SetKinematic:     return "SetKinematic";
    case forge::AnimEventType::RestorePhysics:   return "RestorePhysics";
    case forge::AnimEventType::RootMotionBegin:  return "RootMotionBegin";
    case forge::AnimEventType::RootMotionEnd:    return "RootMotionEnd";
    case forge::AnimEventType::RootMotionScale:  return "RootMotionScale";
    case forge::AnimEventType::StartTrail:       return "StartTrail";
    case forge::AnimEventType::StopTrail:        return "StopTrail";
    default:                                     return "Unknown";
  }
}

unsigned int TaeEditorUI::eventColor(forge::AnimEventType t) {
  switch (t) {
    case forge::AnimEventType::SpawnHitbox:      return IM_COL32(220,  80,  60, 210);
    case forge::AnimEventType::IFrame:           return IM_COL32( 60, 160, 230, 210);
    case forge::AnimEventType::ComboWindow:      return IM_COL32( 80, 200,  90, 210);
    case forge::AnimEventType::ParryWindow:      return IM_COL32(200, 180,  50, 210);
    case forge::AnimEventType::DeflectWindow:    return IM_COL32(200, 140,  50, 210);
    case forge::AnimEventType::HyperArmorBegin:
    case forge::AnimEventType::HyperArmorEnd:    return IM_COL32(160,  80, 200, 210);
    case forge::AnimEventType::SoundOneShot:
    case forge::AnimEventType::SFXSpawn:         return IM_COL32( 80, 160, 160, 210);
    case forge::AnimEventType::ScriptedSequence: return IM_COL32(160, 100, 200, 210);
    case forge::AnimEventType::SetKinematic:
    case forge::AnimEventType::RestorePhysics:   return IM_COL32(160, 160, 160, 210);
    default:                                     return IM_COL32(140, 140, 140, 210);
  }
}

bool TaeEditorUI::isOneShot(forge::AnimEventType t) {
  switch (t) {
    case forge::AnimEventType::SoundOneShot:
    case forge::AnimEventType::SpawnImpactVFX:
    case forge::AnimEventType::SetKinematic:
    case forge::AnimEventType::RestorePhysics:
    case forge::AnimEventType::HyperArmorBegin:
    case forge::AnimEventType::HyperArmorEnd:
    case forge::AnimEventType::RootMotionBegin:
    case forge::AnimEventType::RootMotionEnd:
    case forge::AnimEventType::RootMotionScale:
    case forge::AnimEventType::StartTrail:
    case forge::AnimEventType::StopTrail:
      return true;
    default: return false;
  }
}

bool TaeEditorUI::usesHitboxExtents(forge::AnimEventType t) {
  return t == forge::AnimEventType::SpawnHitbox;
}

bool TaeEditorUI::usesBonePayload(forge::AnimEventType t) {
  return t == forge::AnimEventType::SpawnHitbox;
}

bool TaeEditorUI::usesComboPayload(forge::AnimEventType t) {
  return t == forge::AnimEventType::ComboWindow
      || t == forge::AnimEventType::ParryWindow
      || t == forge::AnimEventType::DeflectWindow;
}

bool TaeEditorUI::usesTextPayload(forge::AnimEventType t) {
  switch (t) {
    case forge::AnimEventType::SoundOneShot:
    case forge::AnimEventType::SFXSpawn:
    case forge::AnimEventType::ScriptedSequence:
    case forge::AnimEventType::SpawnImpactVFX:
    case forge::AnimEventType::RootMotionBegin:
    case forge::AnimEventType::RootMotionScale:
      return true;
    default: return false;
  }
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

TaeEditorUI::TaeEditorUI(TaeEditorApp& app) : m_app(app) {}

// ---------------------------------------------------------------------------
// Top-level draw
// ---------------------------------------------------------------------------

void TaeEditorUI::draw() {
  drawMenuBar();

  // ---- Dialogs (always pumped so they can open/close independently) ----
  drawOpenDialog();
  drawOpenMovesetDialog();
  drawModelLoadDialog();
  drawAnimBrowserDialog();
  drawNewMovesetDialog();
  drawLoadMovesetEditDialog();
  drawSaveMovesetDialog();

  // ---- Main content ----
  if (m_app.isLoaded()) {
    drawWeaponPreviewPanel();
    drawClipInfo();
    drawTimeline();
    drawEventList();
    if (m_app.hasEditableMoveset() && m_showMovesetEditorPanel)
      drawMovesetEditorPanel();
  } else if (m_app.hasModel()) {
    // Model loaded but no clip yet — show a lightweight hint
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({ io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f },
                            ImGuiCond_Always, { 0.5f, 0.5f });
    ImGui::SetNextWindowSize({ 560.0f, 100.0f }, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("##modelready", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove);
    ImGui::TextColored({ 0.4f, 0.9f, 0.4f, 1.0f }, "Model loaded.");
    ImGui::Spacing();
    ImGui::Text("File > Open Animation Browser  (load a GLB/FBX from a folder)");
    ImGui::Text("File > Open Clip               (type a single animation path)");
    ImGui::End();

    if (m_app.hasEditableMoveset() && m_showMovesetEditorPanel)
      drawMovesetEditorPanel();
  } else {
    drawWelcomeScreen();
  }
}

// ---------------------------------------------------------------------------
// Menu bar
// ---------------------------------------------------------------------------

void TaeEditorUI::drawMenuBar() {
  if (!ImGui::BeginMainMenuBar()) return;

  // ---- File ----
  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("Load Reference Model..."))
      m_showModelLoadDialog = true;

    ImGui::Separator();

    if (ImGui::MenuItem("Open Animation Browser...", "Ctrl+O"))
      m_showAnimBrowserDialog = true;
    if (ImGui::MenuItem("Open Clip (path)..."))
      m_showOpenDialog = true;
    if (ImGui::MenuItem("Open Moveset Action..."))
      m_showMovesetDialog = true;

    ImGui::Separator();

    bool hasClip = m_app.isLoaded();
    if (ImGui::MenuItem("Save Sidecar (TAE)", "Ctrl+S", false, hasClip))
      m_app.saveToSidecar();

    ImGui::EndMenu();
  }

  // ---- Moveset ----
  if (ImGui::BeginMenu("Moveset")) {
    if (ImGui::MenuItem("New Moveset..."))
      m_showNewMovesetDialog = true;
    if (ImGui::MenuItem("Load Moveset for Editing..."))
      m_showLoadMovesetEditDialog = true;

    ImGui::Separator();

    bool hasMoveset = m_app.hasEditableMoveset();
    bool dirtyFlag  = m_app.isEditableMovesetDirty();

    // Label shows unsaved indicator
    const char* saveLabel = dirtyFlag ? "Save Moveset *" : "Save Moveset";
    if (ImGui::MenuItem(saveLabel, "Ctrl+Shift+S", false, hasMoveset)) {
      if (m_app.editableMovesetOutputPath().empty())
        m_showSaveMovesetDialog = true;
      else
        m_app.saveMovesetToJson();
    }
    if (ImGui::MenuItem("Save Moveset As...", nullptr, false, hasMoveset))
      m_showSaveMovesetDialog = true;

    ImGui::Separator();

    if (ImGui::MenuItem("Show Moveset Panel", nullptr, &m_showMovesetEditorPanel, hasMoveset))
      {}

    ImGui::EndMenu();
  }

  // ---- View ----
  if (ImGui::BeginMenu("View")) {
    bool showSkel = m_app.showSkeletonOverlay();
    if (ImGui::MenuItem("Skeleton Overlay", nullptr, &showSkel))
      m_app.setShowSkeletonOverlay(showSkel);
    ImGui::MenuItem("Clip Info",    nullptr, nullptr);
    ImGui::MenuItem("Timeline",     nullptr, nullptr);
    ImGui::MenuItem("Event List",   nullptr, nullptr);
    ImGui::EndMenu();
  }

  // ---- Keyboard shortcuts ----
  auto& io = ImGui::GetIO();
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S) && m_app.isLoaded())
    m_app.saveToSidecar();
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O))
    m_showAnimBrowserDialog = true;
  if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S) && m_app.hasEditableMoveset()) {
    if (m_app.editableMovesetOutputPath().empty())
      m_showSaveMovesetDialog = true;
    else
      m_app.saveMovesetToJson();
  }

  // ---- Right side: show loaded clip path ----
  if (m_app.isLoaded()) {
    float available = ImGui::GetContentRegionAvail().x;
    std::string info = m_app.clipPath();
    float textW = ImGui::CalcTextSize(info.c_str()).x;
    if (textW + 16.0f < available) {
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available - textW - 8.0f);
      ImGui::TextDisabled("%s", info.c_str());
    }
  }

  ImGui::EndMainMenuBar();
}

// ---------------------------------------------------------------------------
// Welcome screen (no model loaded)
// ---------------------------------------------------------------------------

void TaeEditorUI::drawWelcomeScreen() {
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos({ io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f },
                          ImGuiCond_Always, { 0.5f, 0.5f });
  ImGui::SetNextWindowSize({ 580.0f, 140.0f }, ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.85f);
  ImGui::Begin("##welcome", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
               ImGuiWindowFlags_NoMove);
  ImGui::TextDisabled("No model or clip loaded.");
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  ImGui::Text("  File > Load Reference Model          load a character GLB/FBX to preview against");
  ImGui::Text("  File > Open Animation Browser        browse a folder of GLB/FBX animations");
  ImGui::Text("  File > Open Moveset Action           load from a registered moveset (movesets.json)");
  ImGui::Spacing();
  ImGui::Text("  Moveset > New Moveset                start building a moveset from scratch");
  ImGui::End();
}

// ---------------------------------------------------------------------------
// Load Reference Model dialog
// ---------------------------------------------------------------------------

void TaeEditorUI::drawModelLoadDialog() {
  if (!m_showModelLoadDialog) return;

  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowSize({ 560.0f, 120.0f }, ImGuiCond_Always);
  ImGui::SetNextWindowPos({ io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f },
                          ImGuiCond_Always, { 0.5f, 0.5f });

  if (!ImGui::Begin("Load Reference Model", &m_showModelLoadDialog,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
    ImGui::End(); return;
  }

  ImGui::Text("Model GLB (relative to asset root):");
  ImGui::SetNextItemWidth(-1.0f);
  if (m_modelPathBuff[0] == '\0' && !m_app.charPath().empty())
    strncpy(m_modelPathBuff, m_app.charPath().c_str(), sizeof(m_modelPathBuff) - 1);

  bool enter = ImGui::InputText("##modelpath", m_modelPathBuff, sizeof(m_modelPathBuff),
                                ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::Spacing();

  float scale = m_app.modelUnitScale();
  if (ImGui::InputFloat("Unit Scale", &scale, 0.01f, 0.1f, "%.3f")) {
    scale = std::clamp(scale, 0.001f, 10.0f);
    m_app.setModelUnitScale(scale);
  }
  ImGui::SameLine();
  if (ImGui::Button("cm (0.01)")) m_app.setModelUnitScale(0.01f);
  ImGui::SameLine();
  if (ImGui::Button("m (1.0)"))   m_app.setModelUnitScale(1.0f);

  ImGui::Spacing();

  bool load = ImGui::Button("Load", { 80.0f, 0.0f });
  ImGui::SameLine();
  if (ImGui::Button("Cancel", { 80.0f, 0.0f }))
    m_showModelLoadDialog = false;

  if ((load || enter) && m_modelPathBuff[0] != '\0') {
    m_app.loadModel(m_modelPathBuff);
    m_app.setStartupChar(m_charPathBuff);
    m_showModelLoadDialog = false;
  }

  ImGui::End();
}

// ---------------------------------------------------------------------------
// Legacy "Open Clip (path)" dialog — still works, now only requires a clip path
// if a model is already loaded, otherwise asks for both.
// ---------------------------------------------------------------------------

void TaeEditorUI::drawOpenDialog() {
  if (!m_showOpenDialog) return;

  ImGuiIO& io = ImGui::GetIO();
  bool needsModel = !m_app.hasModel();
  float winH = needsModel ? 180.0f : 130.0f;

  ImGui::SetNextWindowSize({ 560.0f, winH }, ImGuiCond_Always);
  ImGui::SetNextWindowPos({ io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f },
                          ImGuiCond_Always, { 0.5f, 0.5f });

  if (!ImGui::Begin("Open Clip", &m_showOpenDialog,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
    ImGui::End(); return;
  }

  if (needsModel) {
    ImGui::Text("Character GLB (relative to asset root):");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##charpath", m_charPathBuff, sizeof(m_charPathBuff));
    ImGui::Spacing();
  } else {
    // Pre-fill from current char path so user sees what model is active
    if (m_charPathBuff[0] == '\0' && !m_app.charPath().empty())
      strncpy(m_charPathBuff, m_app.charPath().c_str(), sizeof(m_charPathBuff) - 1);
    ImGui::TextDisabled("Model: %s", m_app.charPath().c_str());
    ImGui::Spacing();
  }

  ImGui::Text("Animation GLB/FBX (relative to asset root):");
  ImGui::SetNextItemWidth(-1.0f);
  bool loadAnim = ImGui::InputText("##clippath", m_clipPathBuff, sizeof(m_clipPathBuff),
                                   ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::Spacing();

  bool clickLoad = ImGui::Button("Load", { 80.0f, 0.0f });
  ImGui::SameLine();
  if (ImGui::Button("Cancel", { 80.0f, 0.0f }))
    m_showOpenDialog = false;

  if (clickLoad || loadAnim) {
    if (m_clipPathBuff[0] != '\0') {
      if (needsModel && m_charPathBuff[0] != '\0')
        m_app.loadClipAndSkeleton(m_charPathBuff, m_clipPathBuff);
      else if (!needsModel)
        m_app.loadClipFromPath(m_clipPathBuff);
      m_showOpenDialog = false;
    }
  }

  ImGui::End();
}

// ---------------------------------------------------------------------------
// Open Moveset Action dialog (unchanged from original, reads from movesets.json)
// ---------------------------------------------------------------------------

void TaeEditorUI::drawOpenMovesetDialog() {
  if (!m_showMovesetDialog) return;

  const auto& allMovesets = forge::AssetManager::getAllMovesetDefs();

  std::vector<const forge::MovesetDef*> sortedMovesets;
  sortedMovesets.reserve(allMovesets.size());
  for (const auto& [id, def] : allMovesets)
    sortedMovesets.push_back(&def);
  std::sort(sortedMovesets.begin(), sortedMovesets.end(),
            [](const forge::MovesetDef* a, const forge::MovesetDef* b) {
              return a->id < b->id;
            });

  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowSize({ 560.0f, 200.0f }, ImGuiCond_Always);
  ImGui::SetNextWindowPos({ io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f },
                          ImGuiCond_Always, { 0.5f, 0.5f });

  if (!ImGui::Begin("Open Moveset Action", &m_showMovesetDialog,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
    ImGui::End(); return;
  }

  if (m_charPathBuff[0] == '\0' && !m_app.charPath().empty())
    strncpy(m_charPathBuff, m_app.charPath().c_str(), sizeof(m_charPathBuff) - 1);

  ImGui::Text("Character GLB (relative to asset root):");
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputText("##charpath_mv", m_charPathBuff, sizeof(m_charPathBuff));
  ImGui::Spacing();

  if (sortedMovesets.empty()) {
    ImGui::TextColored({ 1.0f, 0.4f, 0.4f, 1.0f },
                       "No movesets loaded. Check assets/data/movesets.json");
    if (ImGui::Button("Cancel", { 80.0f, 0.0f }))
      m_showMovesetDialog = false;
    ImGui::End(); return;
  }

  m_selectedMovesetIdx = std::clamp(m_selectedMovesetIdx, 0,
                                    (int)sortedMovesets.size() - 1);
  const forge::MovesetDef* selectedMoveset = sortedMovesets[m_selectedMovesetIdx];

  ImGui::Text("Moveset:");
  ImGui::SameLine(90.0f);
  ImGui::SetNextItemWidth(200.0f);
  if (ImGui::BeginCombo("##mvset", selectedMoveset->id.c_str())) {
    for (int i = 0; i < (int)sortedMovesets.size(); i++) {
      bool isSel = (i == m_selectedMovesetIdx);
      if (ImGui::Selectable(sortedMovesets[i]->id.c_str(), isSel)) {
        if (m_selectedMovesetIdx != i) {
          m_selectedMovesetIdx = i;
          m_selectedActionIdx  = 0;
          selectedMoveset      = sortedMovesets[i];
        }
      }
      if (isSel) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  std::vector<std::string> actionKeys;
  actionKeys.reserve(selectedMoveset->clips.size());
  for (const auto& [key, path] : selectedMoveset->clips)
    actionKeys.push_back(key);
  std::sort(actionKeys.begin(), actionKeys.end());

  m_selectedActionIdx = std::clamp(m_selectedActionIdx, 0,
                                   actionKeys.empty() ? 0 : (int)actionKeys.size() - 1);

  ImGui::Text("Action:");
  ImGui::SameLine(90.0f);

  if (actionKeys.empty()) {
    ImGui::TextDisabled("(No actions defined)");
  } else {
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::BeginCombo("##mvaction", actionKeys[m_selectedActionIdx].c_str())) {
      for (int i = 0; i < (int)actionKeys.size(); i++) {
        bool isSel = (i == m_selectedActionIdx);
        if (ImGui::Selectable(actionKeys[i].c_str(), isSel))
          m_selectedActionIdx = i;
        if (isSel) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    const std::string& resolvedPath = selectedMoveset->clips.at(actionKeys[m_selectedActionIdx]);
    ImGui::SameLine(0.0f, 12.0f);
    ImGui::TextDisabled("%s", resolvedPath.c_str());
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  bool canLoad = m_charPathBuff[0] != '\0' && !actionKeys.empty();
  ImGui::BeginDisabled(!canLoad);
  bool clickLoad = ImGui::Button("Load", { 80.0f, 0.0f });
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel", { 80.0f, 0.0f }))
    m_showMovesetDialog = false;

  if (clickLoad && canLoad) {
    m_app.loadMovesetAction(m_charPathBuff, selectedMoveset->id,
                            actionKeys[m_selectedActionIdx]);
    m_showMovesetDialog = false;
  }

  ImGui::End();
}

// ---------------------------------------------------------------------------
// Animation Browser dialog
// ---------------------------------------------------------------------------

void TaeEditorUI::drawAnimBrowserDialog() {
  if (!m_showAnimBrowserDialog) return;

  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowSize({ 700.0f, 520.0f }, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos({ io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f },
                          ImGuiCond_FirstUseEver, { 0.5f, 0.5f });

  if (!ImGui::Begin("Animation Browser", &m_showAnimBrowserDialog)) {
    ImGui::End(); return;
  }

  // ---- Directory input + scan ----
  ImGui::Text("Directory (relative to asset root, or absolute):");
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f);
  if (m_animBrowserDirBuff[0] == '\0' && !m_app.animDir().empty())
    strncpy(m_animBrowserDirBuff, m_app.animDir().c_str(), sizeof(m_animBrowserDirBuff) - 1);
  bool scanEnter = ImGui::InputText("##animdir", m_animBrowserDirBuff,
                                    sizeof(m_animBrowserDirBuff),
                                    ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();
  bool scanClick = ImGui::Button("Scan", { 64.0f, 0.0f });
  if (scanEnter || scanClick) {
    scanAnimFolder(m_animBrowserDirBuff);
    m_app.setAnimDir(m_animBrowserDirBuff);
  }

  ImGui::Spacing();

  // ---- Filter ----
  ImGui::Text("Filter:");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(200.0f);
  ImGui::InputText("##animfilter", m_animBrowserFilterBuff, sizeof(m_animBrowserFilterBuff));
  ImGui::SameLine();
  if (ImGui::SmallButton("Clear##filter"))
    m_animBrowserFilterBuff[0] = '\0';

  // ---- Mode toggle ----
  ImGui::SameLine(0.0f, 24.0f);
  if (m_app.hasEditableMoveset()) {
    ImGui::Checkbox("Add to Moveset", &m_animBrowserAddToMoveset);
  } else {
    ImGui::BeginDisabled(true);
    ImGui::Checkbox("Add to Moveset (no moveset loaded)", &m_animBrowserAddToMoveset);
    ImGui::EndDisabled();
    m_animBrowserAddToMoveset = false;
  }

  ImGui::Separator();

  // ---- File list ----
  std::string filterStr = m_animBrowserFilterBuff;
  std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::tolower);

  float listH = ImGui::GetContentRegionAvail().y - (m_animBrowserAddToMoveset ? 110.0f : 62.0f);
  ImGui::BeginChild("##animlist", { 0.0f, listH }, true);

  if (m_animBrowserFiles.empty()) {
    ImGui::TextDisabled("No .glb or .fbx files found. Enter a directory above and press Scan.");
  } else {
    for (int i = 0; i < (int)m_animBrowserFiles.size(); i++) {
      const std::string& displayName = m_animBrowserDisplayNames[i];

      // Apply filter
      if (!filterStr.empty()) {
        std::string lower = displayName;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.find(filterStr) == std::string::npos)
          continue;
      }

      bool isSelected = (m_animBrowserSelectedIdx == i);
      if (ImGui::Selectable(displayName.c_str(), isSelected,
                            ImGuiSelectableFlags_AllowDoubleClick)) {
        m_animBrowserSelectedIdx = i;

        // Double-click = immediate load (not add-to-moveset)
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !m_animBrowserAddToMoveset) {
          applyAnimBrowserSelection(m_animBrowserFiles[i]);
          m_showAnimBrowserDialog = false;
        }
      }

      // Tooltip: full relative path
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", m_animBrowserFiles[i].c_str());
    }
  }
  ImGui::EndChild();

  // ---- Action key input (only shown in moveset-add mode) ----
  if (m_animBrowserAddToMoveset) {
    ImGui::Spacing();
    ImGui::Text("Action key:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputText("##pendingkey", m_pendingActionKeyBuff, sizeof(m_pendingActionKeyBuff));

    // Auto-fill key from selected filename stem when selection changes
    if (m_animBrowserSelectedIdx >= 0 && m_pendingActionKeyBuff[0] == '\0') {
      const std::string& fname = m_animBrowserDisplayNames[m_animBrowserSelectedIdx];
      fs::path p(fname);
      std::string stem = p.stem().string();
      strncpy(m_pendingActionKeyBuff, stem.c_str(), sizeof(m_pendingActionKeyBuff) - 1);
    }
  }

  // ---- Buttons ----
  ImGui::Spacing();
  bool hasSelection = (m_animBrowserSelectedIdx >= 0 &&
                       m_animBrowserSelectedIdx < (int)m_animBrowserFiles.size());
  bool hasModel     = m_app.hasModel();
  bool keyIsSet     = m_pendingActionKeyBuff[0] != '\0';

  if (m_animBrowserAddToMoveset) {
    ImGui::BeginDisabled(!hasSelection || !hasModel || !keyIsSet);
    if (ImGui::Button("Add to Moveset + Preview", { 220.0f, 0.0f })) {
      const std::string& relPath = m_animBrowserFiles[m_animBrowserSelectedIdx];
      m_app.addEntryToMoveset(m_pendingActionKeyBuff, relPath);
      applyAnimBrowserSelection(relPath);
      m_pendingActionKeyBuff[0] = '\0'; // clear for next entry
      // Stay open so user can add more
    }
    ImGui::EndDisabled();
  } else {
    ImGui::BeginDisabled(!hasSelection || !hasModel);
    if (ImGui::Button("Load Animation", { 140.0f, 0.0f })) {
      applyAnimBrowserSelection(m_animBrowserFiles[m_animBrowserSelectedIdx]);
      m_showAnimBrowserDialog = false;
    }
    ImGui::EndDisabled();

    if (!hasModel) {
      ImGui::SameLine();
      ImGui::TextColored({ 1.0f, 0.5f, 0.3f, 1.0f },
                         "Load a Reference Model first (File menu)");
    }
  }

  ImGui::SameLine();
  if (ImGui::Button("Close", { 80.0f, 0.0f }))
    m_showAnimBrowserDialog = false;

  ImGui::End();
}

// ---------------------------------------------------------------------------
// Moveset Editor panel
// ---------------------------------------------------------------------------

void TaeEditorUI::drawMovesetEditorPanel() {
  ImGui::SetNextWindowPos({ (float)ImGui::GetIO().DisplaySize.x - 400.0f, 26.0f },
                          ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({ 390.0f, 600.0f }, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.93f);

  std::string title = "Moveset Editor";
  if (m_app.isEditableMovesetDirty()) title += " *";
  title += "##movesetpanel";

  if (!ImGui::Begin(title.c_str(), &m_showMovesetEditorPanel)) {
    ImGui::End(); return;
  }

  // ---- Header ----
  ImGui::TextColored({ 0.9f, 0.75f, 0.3f, 1.0f }, "ID: %s",
                     m_app.editableMovesetId().c_str());
  ImGui::SameLine();
  if (!m_app.editableMovesetOutputPath().empty()) {
    ImGui::TextDisabled(" → %s", m_app.editableMovesetOutputPath().c_str());
  } else {
    ImGui::TextDisabled(" (unsaved)");
  }
  ImGui::Separator();
  ImGui::Spacing();

  // ---- Entry list ----
  auto& entries = m_app.editableMoveset();
  float listH   = ImGui::GetContentRegionAvail().y - 68.0f;

  ImGuiTableFlags tflags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                         | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;

  if (ImGui::BeginTable("##msentries", 4, tflags, { 0.0f, listH })) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Action Key",  ImGuiTableColumnFlags_WidthFixed,   120.0f);
    ImGui::TableSetupColumn("Clip",        ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("##preview",   ImGuiTableColumnFlags_WidthFixed,    52.0f);
    ImGui::TableSetupColumn("##remove",    ImGuiTableColumnFlags_WidthFixed,    22.0f);
    ImGui::TableHeadersRow();

    int removeIdx = -1;
    for (int i = 0; i < (int)entries.size(); i++) {
      auto& entry = entries[i];
      ImGui::PushID(i);
      ImGui::TableNextRow();

      // Col 0: Editable action key
      ImGui::TableSetColumnIndex(0);
      ImGui::SetNextItemWidth(-1.0f);
      char keyBuf[128] = {};
      strncpy(keyBuf, entry.actionKey.c_str(), sizeof(keyBuf) - 1);
      if (ImGui::InputText("##key", keyBuf, sizeof(keyBuf),
                           ImGuiInputTextFlags_EnterReturnsTrue)) {
        m_app.renameEntryKey(i, keyBuf);
      }

      // Col 1: Clip path (truncated, tooltip for full path)
      ImGui::TableSetColumnIndex(1);
      std::string shortPath = entry.clipPath;
      // Show only last two path components for brevity
      fs::path p(entry.clipPath);
      if (p.has_parent_path())
        shortPath = (p.parent_path().filename() / p.filename()).generic_string();
      ImGui::TextUnformatted(shortPath.c_str());
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", entry.clipPath.c_str());

      // Col 2: Preview button
      ImGui::TableSetColumnIndex(2);
      if (ImGui::SmallButton("Preview")) {
        if (m_app.hasModel())
          m_app.loadClipFromPath(entry.clipPath, entry.actionKey);
      }

      // Col 3: Remove button
      ImGui::TableSetColumnIndex(3);
      if (ImGui::SmallButton("×"))
        removeIdx = i;

      ImGui::PopID();
    }

    if (removeIdx >= 0)
      m_app.removeEntryFromMoveset(removeIdx);

    ImGui::EndTable();
  }

  ImGui::Spacing();

  // ---- Bottom bar ----
  if (ImGui::Button("Add Animation...", { 140.0f, 0.0f })) {
    m_animBrowserAddToMoveset = true;
    m_showAnimBrowserDialog   = true;
  }
  ImGui::SameLine();

  bool dirty = m_app.isEditableMovesetDirty();
  ImGui::BeginDisabled(!dirty);
  if (ImGui::Button("Save", { 60.0f, 0.0f })) {
    if (m_app.editableMovesetOutputPath().empty())
      m_showSaveMovesetDialog = true;
    else
      m_app.saveMovesetToJson();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Save As...", { 80.0f, 0.0f }))
    m_showSaveMovesetDialog = true;

  ImGui::End();
}

// ---------------------------------------------------------------------------
// New Moveset dialog
// ---------------------------------------------------------------------------

void TaeEditorUI::drawNewMovesetDialog() {
  if (!m_showNewMovesetDialog) return;

  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowSize({ 560.0f, 160.0f }, ImGuiCond_Always);
  ImGui::SetNextWindowPos({ io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f },
                          ImGuiCond_Always, { 0.5f, 0.5f });

  if (!ImGui::Begin("New Moveset", &m_showNewMovesetDialog,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
    ImGui::End(); return;
  }

  ImGui::Text("Moveset ID (e.g. \"sword\"):");
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputText("##newid", m_newMovesetIdBuff, sizeof(m_newMovesetIdBuff));
  ImGui::Spacing();

  ImGui::Text("Output path (relative to asset root):");
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputText("##newpath", m_newMovesetOutputPathBuff, sizeof(m_newMovesetOutputPathBuff));

  ImGui::Spacing();
  ImGui::TextDisabled("Tip: e.g. data/movesets/greatsword.moveset.json");
  ImGui::Spacing();

  bool canCreate = m_newMovesetIdBuff[0] != '\0';
  ImGui::BeginDisabled(!canCreate);
  if (ImGui::Button("Create", { 80.0f, 0.0f })) {
    m_app.newMoveset(m_newMovesetIdBuff, m_newMovesetOutputPathBuff);
    m_showMovesetEditorPanel   = true;
    m_showNewMovesetDialog     = false;
    m_newMovesetIdBuff[0]      = '\0';
    m_newMovesetOutputPathBuff[0] = '\0';
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel", { 80.0f, 0.0f }))
    m_showNewMovesetDialog = false;

  ImGui::End();
}

// ---------------------------------------------------------------------------
// Load Moveset for Editing dialog
// ---------------------------------------------------------------------------

void TaeEditorUI::drawLoadMovesetEditDialog() {
  if (!m_showLoadMovesetEditDialog) return;

  const auto& allMovesets = forge::AssetManager::getAllMovesetDefs();
  std::vector<const forge::MovesetDef*> sorted;
  sorted.reserve(allMovesets.size());
  for (const auto& [id, def] : allMovesets)
    sorted.push_back(&def);
  std::sort(sorted.begin(), sorted.end(),
            [](const forge::MovesetDef* a, const forge::MovesetDef* b) {
              return a->id < b->id;
            });

  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowSize({ 420.0f, 140.0f }, ImGuiCond_Always);
  ImGui::SetNextWindowPos({ io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f },
                          ImGuiCond_Always, { 0.5f, 0.5f });

  if (!ImGui::Begin("Load Moveset for Editing", &m_showLoadMovesetEditDialog,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
    ImGui::End(); return;
  }

  if (sorted.empty()) {
    ImGui::TextColored({ 1.0f, 0.4f, 0.4f, 1.0f }, "No movesets in movesets.json");
    if (ImGui::Button("Close")) m_showLoadMovesetEditDialog = false;
    ImGui::End(); return;
  }

  m_loadMovesetEditIdx = std::clamp(m_loadMovesetEditIdx, 0, (int)sorted.size() - 1);

  ImGui::Text("Select moveset:");
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::BeginCombo("##editms", sorted[m_loadMovesetEditIdx]->id.c_str())) {
    for (int i = 0; i < (int)sorted.size(); i++) {
      bool sel = (i == m_loadMovesetEditIdx);
      if (ImGui::Selectable(sorted[i]->id.c_str(), sel))
        m_loadMovesetEditIdx = i;
      if (sel) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  ImGui::TextDisabled("%d action(s) defined",
                      (int)sorted[m_loadMovesetEditIdx]->clips.size());
  ImGui::Spacing();

  if (ImGui::Button("Load for Editing", { 140.0f, 0.0f })) {
    m_app.loadMovesetForEditing(sorted[m_loadMovesetEditIdx]->id);
    m_showMovesetEditorPanel     = true;
    m_showLoadMovesetEditDialog  = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", { 80.0f, 0.0f }))
    m_showLoadMovesetEditDialog = false;

  ImGui::End();
}

// ---------------------------------------------------------------------------
// Save Moveset As dialog
// ---------------------------------------------------------------------------

void TaeEditorUI::drawSaveMovesetDialog() {
  if (!m_showSaveMovesetDialog) return;

  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowSize({ 560.0f, 120.0f }, ImGuiCond_Always);
  ImGui::SetNextWindowPos({ io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f },
                          ImGuiCond_Always, { 0.5f, 0.5f });

  if (!ImGui::Begin("Save Moveset As", &m_showSaveMovesetDialog,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
    ImGui::End(); return;
  }

  // Pre-fill from existing output path
  if (m_saveMovesetPathBuff[0] == '\0' && !m_app.editableMovesetOutputPath().empty())
    strncpy(m_saveMovesetPathBuff, m_app.editableMovesetOutputPath().c_str(),
            sizeof(m_saveMovesetPathBuff) - 1);

  ImGui::Text("Output path (relative to asset root):");
  ImGui::SetNextItemWidth(-1.0f);
  bool enter = ImGui::InputText("##savepath", m_saveMovesetPathBuff,
                                sizeof(m_saveMovesetPathBuff),
                                ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::Spacing();
  ImGui::TextDisabled("e.g.  data/movesets/sword.moveset.json");
  ImGui::Spacing();

  bool save = ImGui::Button("Save", { 80.0f, 0.0f });
  ImGui::SameLine();
  if (ImGui::Button("Cancel", { 80.0f, 0.0f }))
    m_showSaveMovesetDialog = false;

  if ((save || enter) && m_saveMovesetPathBuff[0] != '\0') {
    m_app.setEditableMovesetOutputPath(m_saveMovesetPathBuff);
    m_app.saveMovesetToJson(m_saveMovesetPathBuff);
    m_showSaveMovesetDialog   = false;
    m_saveMovesetPathBuff[0]  = '\0';
  }

  ImGui::End();
}

// ---------------------------------------------------------------------------
// Weapon preview panel
// ---------------------------------------------------------------------------

void TaeEditorUI::drawWeaponPreviewPanel() {
  if (!ImGui::Begin("Weapon Preview")) { ImGui::End(); return; }

  const forge::WeaponDef* active = m_app.getActiveWeapon();
  const char* previewLabel = active ? active->id.c_str() : "None";

  ImGui::SetNextItemWidth(160.0f);
  if (ImGui::BeginCombo("##wpn_select", previewLabel)) {
    // None always first
    if (ImGui::Selectable("None", active == nullptr))
      m_app.clearWeapon();

    for (const auto& [id, def] : forge::AssetManager::getAllWeaponDefs()) {
      bool selected = (active && active->id == id);
      if (ImGui::Selectable(id.c_str(), selected))
        m_app.loadWeapon(id);
      if (selected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  if (active) {
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
      m_app.clearWeapon();

    ImGui::Spacing();

    int boneIdx = m_app.getWeaponBoneIndex();
    if (boneIdx >= 0) {
      ImGui::TextDisabled("Bone: %s  [%d]", active->boneAttach.c_str(), boneIdx);
    } else {
      ImGui::TextColored({ 1.0f, 0.35f, 0.35f, 1.0f },
                         "Bone '%s' not in skeleton", active->boneAttach.c_str());
    }

    // Offset editor
    ImGui::Separator();
    ImGui::Text("Mesh Offset");
    ImGui::Spacing();

    auto& edit = m_app.getOffsetEdit();
    bool changed = false;

    changed |= ImGui::DragFloat3("Position (m)", &edit.pos.x, 0.001f);
    changed |= ImGui::DragFloat3("Rotation (deg)", &edit.euler.x, 0.25f);
    changed |= ImGui::DragFloat3("Scale", &edit.scale.x, 0.01f, 0.01f, 10.0f);

    if (changed) edit.dirty = true;

    ImGui::Spacing();

    const char* saveLabel = edit.dirty ? "Save to weapons.json *" : "Save to weapons.json";
    if (ImGui::Button(saveLabel))
      m_app.saveWeaponOffset();

    ImGui::SameLine();
    if (ImGui::Button("Reset"))
      m_app.resetWeaponOffset();

    if (edit.dirty)
      ImGui::TextColored({ 1.0f, 0.8f, 0.3f, 1.0f }, "Unsaved changes");
  } else {
    ImGui::TextDisabled("Select a weapon to preview it in the viewport.");
  }

  ImGui::End();
}

// ---------------------------------------------------------------------------
// Clip Info panel (unchanged from original)
// ---------------------------------------------------------------------------

void TaeEditorUI::drawClipInfo() {
  ImGui::SetNextWindowPos({ 8.0f, 26.0f }, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({ 320.0f, 0.0f }, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.88f);

  if (!ImGui::Begin("Clip Info", nullptr,
                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
    ImGui::End(); return;
  }

  forge::AnimationClip* clip = m_app.clip();

  if (!m_app.activeActionKey().empty()) {
    ImGui::TextColored({ 0.9f, 0.75f, 0.3f, 1.0f }, "%s",
                       m_app.activeMovesetId().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(" \xe2\x86\x92 ");
    ImGui::SameLine();
    ImGui::TextColored({ 0.9f, 0.75f, 0.3f, 1.0f }, "%s",
                       m_app.activeActionKey().c_str());
  } else {
    ImGui::TextColored({ 0.7f, 0.9f, 0.7f, 1.0f }, "%s", clip->name.c_str());
  }

  ImGui::Text("Duration: %.3f s  |   Events: %d",
              clip->duration,
              (int)m_app.events().size());

  ImGui::Spacing();

  // Playback controls
  bool playing = m_app.isPlaying();
  if (ImGui::Button(playing ? "Pause" : "Play ", { 60.0f, 0.0f }))
    m_app.togglePlaying();

  ImGui::SameLine();
  float scrub = m_app.scrubTime();
  ImGui::SetNextItemWidth(180.0f);
  if (ImGui::SliderFloat("##scrub", &scrub, 0.0f, clip->duration, "%.3f s"))
    m_app.setScrubTime(scrub);

  ImGui::End();
}

// ---------------------------------------------------------------------------
// Timeline (unchanged from original)
// ---------------------------------------------------------------------------

void TaeEditorUI::drawTimeline() {
  ImGui::SetNextWindowPos({ 8.0f, 180.0f }, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({ 900.0f, 200.0f }, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.92f);

  if (!ImGui::Begin("Timeline", nullptr, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End(); return;
  }

  forge::AnimationClip* clip = m_app.clip();
  auto& events = m_app.events();
  int& selIdx  = m_app.selectedEvent();

  const float RULER_H   = 20.0f;
  const float TRACK_H   = 18.0f;
  const float LABEL_W   = 130.0f;
  const float GRAB_W    = 5.0f;
  const float TICK_RADIUS = 5.0f;

  float trackW = ImGui::GetContentRegionAvail().x - LABEL_W;
  if (trackW < 1.0f) { ImGui::End(); return; }

  float pxPerSec  = trackW / clip->duration;
  ImDrawList* dl  = ImGui::GetWindowDrawList();
  ImVec2 cp       = ImGui::GetCursorScreenPos();

  auto timeToX = [&](float t) { return cp.x + LABEL_W + t * pxPerSec; };
  auto xToTime = [&](float x) {
    return std::clamp((x - cp.x - LABEL_W) / pxPerSec, 0.0f, clip->duration);
  };

  // Ruler
  float rulerBottom = cp.y + RULER_H;
  dl->AddRectFilled({ cp.x + LABEL_W, cp.y }, { cp.x + LABEL_W + trackW, rulerBottom },
                    IM_COL32(40, 40, 45, 255));

  int numTicks = std::max(2, (int)(clip->duration * 10));
  float tickStep = clip->duration / numTicks;
  for (int i = 0; i <= numTicks; i++) {
    float t = i * tickStep;
    float x = timeToX(t);
    bool isMajor = (i % 5 == 0);
    float th = isMajor ? RULER_H * 0.6f : RULER_H * 0.35f;
    dl->AddLine({ x, rulerBottom - th }, { x, rulerBottom },
                IM_COL32(180, 180, 180, 200));
    if (isMajor) {
      char buf[16]; snprintf(buf, sizeof(buf), "%.2f", t);
      dl->AddText({ x + 2.0f, cp.y + 2.0f }, IM_COL32(200, 200, 200, 220), buf);
    }
  }

  // Event tracks
  for (int i = 0; i < (int)events.size(); i++) {
    forge::AnimEvent& ev = events[i];
    float rowTop = rulerBottom + i * TRACK_H;

    // Label
    ImU32 labelCol = (selIdx == i) ? IM_COL32(200, 200, 255, 255)
                                   : IM_COL32(180, 180, 180, 200);
    dl->AddText({ cp.x + 2.0f, rowTop + 2.0f }, labelCol, eventTypeName(ev.type));

    // Bar
    float x0 = timeToX(ev.startTime);
    float x1 = isOneShot(ev.type) ? x0 + 4.0f : timeToX(ev.endTime);
    float barW = x1 - x0;

    dl->AddRectFilled({ x0, rowTop + 2.0f }, { x1, rowTop + TRACK_H - 2.0f },
                      eventColor(ev.type), 2.0f);

    if (selIdx == i)
      dl->AddRect({ x0, rowTop + 1.0f }, { x1, rowTop + TRACK_H - 1.0f },
                  IM_COL32(255, 255, 255, 180), 2.0f, 0, 1.5f);

    ImGui::PushID(i);

    if (!isOneShot(ev.type)) {
      // Left edge resize
      ImGui::SetCursorScreenPos({ x0, rowTop });
      ImGui::InvisibleButton("##le", { GRAB_W, TRACK_H });
      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ev.startTime = std::clamp(ev.startTime + ImGui::GetIO().MouseDelta.x / pxPerSec,
                                  0.0f, ev.endTime - 0.005f);
      }
      if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

      // Center (select + move)
      float centerX = x0 + GRAB_W;
      float centerW = barW - 2.0f * GRAB_W;
      if (centerW > 1.0f) {
        ImGui::SetCursorScreenPos({ centerX, rowTop });
        ImGui::InvisibleButton("##ctr", { centerW, TRACK_H });
        if (ImGui::IsItemClicked()) {
          selIdx = i;
          m_app.setScrubTime(ev.startTime);
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
          float len   = ev.endTime - ev.startTime;
          float delta = ImGui::GetIO().MouseDelta.x / pxPerSec;
          ev.startTime = std::clamp(ev.startTime + delta, 0.0f, clip->duration - len);
          ev.endTime   = ev.startTime + len;
        }
      }

      // Right edge resize
      ImGui::SetCursorScreenPos({ x1 - GRAB_W, rowTop });
      ImGui::InvisibleButton("##re", { GRAB_W, TRACK_H });
      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ev.endTime = std::clamp(ev.endTime + ImGui::GetIO().MouseDelta.x / pxPerSec,
                                ev.startTime + 0.005f, clip->duration);
      }
      if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    ImGui::PopID();
  }

  // Scrub line
  float scrubX      = timeToX(m_app.scrubTime());
  float trackBottom = rulerBottom + (float)events.size() * TRACK_H;
  dl->AddLine({ scrubX, rulerBottom }, { scrubX, trackBottom },
              IM_COL32(255, 210, 50, 230), 1.5f);
  dl->AddCircleFilled({ scrubX, rulerBottom + TICK_RADIUS }, TICK_RADIUS,
                      IM_COL32(255, 210, 50, 255));

  // Ruler click/drag
  ImGui::SetCursorScreenPos({ cp.x + LABEL_W, cp.y });
  ImGui::InvisibleButton("##ruler", { trackW, RULER_H });
  if (ImGui::IsItemActive())
    m_app.setScrubTime(xToTime(ImGui::GetMousePos().x));

  ImGui::SetCursorScreenPos({ cp.x, trackBottom });
  ImGui::Dummy({ 0.0f, 4.0f });

  ImGui::End();
}

// ---------------------------------------------------------------------------
// Event List panel (unchanged from original)
// ---------------------------------------------------------------------------

void TaeEditorUI::drawEventList() {
  ImGui::SetNextWindowPos({ 8.0f, 130.0f }, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({ 560.0f, 360.0f }, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.92f);

  if (!ImGui::Begin("Events", nullptr, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End(); return;
  }

  forge::AnimationClip* clip = m_app.clip();
  auto& events = m_app.events();
  int& selIdx  = m_app.selectedEvent();

  ImGuiTableFlags tflags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                         | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;

  float tableH = ImGui::GetContentRegionAvail().y - 36.0f;

  if (ImGui::BeginTable("##evtable", 6, tflags, { 0.0f, tableH })) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Type",      ImGuiTableColumnFlags_WidthFixed,   140.0f);
    ImGui::TableSetupColumn("Start (s)", ImGuiTableColumnFlags_WidthFixed,    70.0f);
    ImGui::TableSetupColumn("End (s)",   ImGuiTableColumnFlags_WidthFixed,    70.0f);
    ImGui::TableSetupColumn("Payload",   ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Extents",   ImGuiTableColumnFlags_WidthFixed,   160.0f);
    ImGui::TableSetupColumn("##del",     ImGuiTableColumnFlags_WidthFixed,    20.0f);
    ImGui::TableHeadersRow();

    for (int i = 0; i < (int)events.size(); i++) {
      forge::AnimEvent& ev = events[i];
      bool selected = (selIdx == i);

      ImGui::PushID(i);
      ImGui::TableNextRow();

      if (selected)
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(55, 75, 115, 160));

      // Type dropdown
      ImGui::TableSetColumnIndex(0);
      ImGui::SetNextItemWidth(-1.0f);
      if (ImGui::BeginCombo("##type", eventTypeName(ev.type))) {
        for (int t = 0; t < k_eventTypeCount; t++) {
          bool isSel = (ev.type == k_allEventTypes[t]);
          if (ImGui::Selectable(eventTypeName(k_allEventTypes[t]), isSel))
            ev.type = k_allEventTypes[t];
          if (isSel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      // Start time
      ImGui::TableSetColumnIndex(1);
      ImGui::SetNextItemWidth(-1.0f);
      bool oneShotType = isOneShot(ev.type);
      if (ImGui::DragFloat("##st", &ev.startTime, 0.001f, 0.0f,
                           oneShotType ? clip->duration : ev.endTime - 0.001f, "%.3f")) {
        if (oneShotType) ev.endTime = ev.startTime;
        selIdx = i;
        m_app.setScrubTime(ev.startTime);
      }

      // End time
      ImGui::TableSetColumnIndex(2);
      ImGui::SetNextItemWidth(-1.0f);
      if (oneShotType) {
        ImGui::BeginDisabled(true);
        ImGui::DragFloat("##et_dis", &ev.endTime, 0.001f, 0.0f, clip->duration, "-");
        ImGui::EndDisabled();
      } else {
        if (ImGui::DragFloat("##et", &ev.endTime, 0.001f,
                             ev.startTime + 0.001f, clip->duration, "%.3f"))
          selIdx = i;
      }

      // Payload
      ImGui::TableSetColumnIndex(3);
      ImGui::SetNextItemWidth(-1.0f);
      if (usesBonePayload(ev.type)) {
        bool valid = !ev.payload.empty() &&
          std::find(m_app.knownBones().begin(), m_app.knownBones().end(),
                    ev.payload) != m_app.knownBones().end();
        bool pushed = !valid && !ev.payload.empty();
        if (pushed) ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(140, 40, 40, 120));
        if (ImGui::BeginCombo("##paybonecombo", ev.payload.empty() ? "-" : ev.payload.c_str())) {
          for (const auto& bone : m_app.knownBones())
            if (ImGui::Selectable(bone.c_str(), ev.payload == bone))
              ev.payload = bone;
          ImGui::EndCombo();
        }
        if (pushed) ImGui::PopStyleColor();
      } else if (usesComboPayload(ev.type)) {
        bool valid = !ev.payload.empty() &&
          std::find(m_app.knownComboKeys().begin(), m_app.knownComboKeys().end(),
                    ev.payload) != m_app.knownComboKeys().end();
        bool pushed = !valid && !ev.payload.empty();
        if (pushed) ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(140, 40, 40, 120));
        if (ImGui::BeginCombo("##paycombocombo", ev.payload.empty() ? "-" : ev.payload.c_str())) {
          for (const auto& key : m_app.knownComboKeys())
            if (ImGui::Selectable(key.c_str(), ev.payload == key))
              ev.payload = key;
          ImGui::EndCombo();
        }
        if (pushed) ImGui::PopStyleColor();
      } else if (usesTextPayload(ev.type)) {
        char buff[256] = {};
        strncpy(buff, ev.payload.c_str(), sizeof(buff) - 1);
        if (ImGui::InputText("##paytxt", buff, sizeof(buff)))
          ev.payload = buff;
      } else {
        ImGui::BeginDisabled(true);
        ImGui::TextUnformatted("-");
        ImGui::EndDisabled();
      }

      // Hitbox extents
      ImGui::TableSetColumnIndex(4);
      if (ev.type == forge::AnimEventType::SpawnHitbox) {
        ImGui::Text("%d seg%s", (int)ev.capsules.size(),
                    ev.capsules.size() == 1 ? "" : "s");
        ImGui::SameLine();
        if (ImGui::SmallButton("Edit##capsules")) {
          m_editingCapsuleEventIdx = selIdx;
          ImGui::OpenPopup("CapsuleSegments");
        }

        if (ImGui::BeginPopup("CapsuleSegments")) {
          auto& events = m_app.events();
          if (m_editingCapsuleEventIdx >= 0 &&
              m_editingCapsuleEventIdx < (int)events.size()) {
            auto& ev = events[m_editingCapsuleEventIdx];
            ImGui::Text("Capsule segments - %s [%.2fs-%.2fs]",
                        ev.payload.c_str(), ev.startTime, ev.endTime);
            ImGui::Separator();

            for (int s = 0; s < (int)ev.capsules.size(); s++) {
              auto& seg = ev.capsules[s];
              ImGui::PushID(s);

              ImGui::Text("Seg %d", s);
              ImGui::SameLine(60.0f);
              ImGui::SetNextItemWidth(180.0f);
              ImGui::DragFloat3("P0##cp0", &seg.localP0.x, 0.005f, -5.0f, 5.0f, "%.3f");
              ImGui::SameLine();
              ImGui::SetNextItemWidth(180.0f);
              ImGui::DragFloat3("P1##cp1", &seg.localP1.x, 0.005f, -5.0f, 5.0f, "%.3f");
              ImGui::SameLine();
              ImGui::SetNextItemWidth(70.0f);
              ImGui::DragFloat("R##cr", &seg.radius, 0.002f, 0.005f, 1.0f, "%.3f");
              ImGui::SameLine();
              if (ImGui::SmallButton("x")) {
                ev.capsules.erase(ev.capsules.begin() + s);
                ImGui::PopID();
                break;
              }

              ImGui::PopID();
            }

            ImGui::Spacing();
            if (ImGui::Button("+ Add Segment")) {
              forge::CapsuleSegment newSeg;
              if (!ev.capsules.empty()) {
                newSeg.localP0 = ev.capsules.back().localP1;
                newSeg.localP1 = newSeg.localP0 + glm::vec3(0.0f, 0.5f, 0.0f);
                newSeg.radius = ev.capsules.back().radius;
              }
              ev.capsules.push_back(newSeg);
            }
          }
          ImGui::EndPopup();
        }
      } else {
        ImGui::BeginDisabled(true);
        ImGui::TextUnformatted("-");
        ImGui::EndDisabled();
      }

      // Delete
      ImGui::TableSetColumnIndex(5);
      if (ImGui::SmallButton("×")) {
        events.erase(events.begin() + i);
        if (selIdx >= (int)events.size()) selIdx = (int)events.size() - 1;
        ImGui::PopID();
        break;
      }

      ImGui::TableSetColumnIndex(0);
      if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        selIdx = i;
        m_app.setScrubTime(ev.startTime);
      }

      ImGui::PopID();
    }
    ImGui::EndTable();
  }


  if (ImGui::Button("Add Event")) {
    forge::AnimEvent ev;
    ev.type         = forge::AnimEventType::SpawnHitbox;
    ev.startTime    = m_app.scrubTime();
    ev.endTime      = std::min(m_app.scrubTime() + 0.1f, clip->duration);
    ev.capsules.push_back({ {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.08f });
    events.push_back(ev);
    selIdx = (int)events.size() - 1;
  }
  ImGui::SameLine();
  if (ImGui::Button("Save Sidecar"))
    m_app.saveToSidecar();

  ImGui::End();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void TaeEditorUI::scanAnimFolder(const std::string& dirPath) {
  m_animBrowserFiles.clear();
  m_animBrowserDisplayNames.clear();
  m_animBrowserSelectedIdx = -1;

  if (dirPath.empty()) return;

  // Resolve: try as absolute first, then relative to asset root
  std::string absDir = dirPath;
  if (!fs::exists(absDir))
    absDir = forge::AssetManager::resolvePath(dirPath);

  if (!fs::is_directory(absDir)) {
    LOG_WARN("[TaeEditor] Not a directory: {}", absDir);
    return;
  }

  const std::string& assetRoot = forge::AssetManager::getAssetRoot();

  for (const auto& entry : fs::directory_iterator(absDir)) {
    if (!entry.is_regular_file()) continue;
    std::string ext = entry.path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext != ".glb" && ext != ".fbx") continue;

    std::string abs = entry.path().generic_string();
    std::string rel = abs;
    if (abs.find(assetRoot) == 0)
      rel = abs.substr(assetRoot.size());

    m_animBrowserFiles.push_back(rel);
    m_animBrowserDisplayNames.push_back(entry.path().filename().string());
  }

  // Sort by display name, keeping both vectors in sync
  std::vector<int> idx(m_animBrowserFiles.size());
  std::iota(idx.begin(), idx.end(), 0);
  std::sort(idx.begin(), idx.end(), [&](int a, int b) {
    return m_animBrowserDisplayNames[a] < m_animBrowserDisplayNames[b];
  });

  std::vector<std::string> sf, sn;
  sf.reserve(idx.size()); sn.reserve(idx.size());
  for (int i : idx) {
    sf.push_back(std::move(m_animBrowserFiles[i]));
    sn.push_back(std::move(m_animBrowserDisplayNames[i]));
  }
  m_animBrowserFiles        = std::move(sf);
  m_animBrowserDisplayNames = std::move(sn);

  LOG_INFO("[TaeEditor] Scanned '{}': {} animation file(s)", dirPath,
           m_animBrowserFiles.size());
}

void TaeEditorUI::applyAnimBrowserSelection(const std::string& relPath) {
  if (!m_app.hasModel()) {
    LOG_WARN("[TaeEditor] applyAnimBrowserSelection: no model loaded");
    return;
  }
  m_app.loadClipFromPath(relPath);
}
