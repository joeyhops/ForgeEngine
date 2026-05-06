#include "TaeEditorUI.h"
#include "TaeEditorApp.h"

#include <forge/AssetManager.h>
#include <forge/MovesetDef.h>
#include <forge/AnimationClip.h>
#include <forge/Animator.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

// k_allEventTypes must match AnimEventType exactly - no COUNT sentinel exists
static const forge::AnimEventType k_allEventTypes[] = {
  forge::AnimEventType::SpawnHitbox,
  forge::AnimEventType::IFrame,
  forge::AnimEventType::SoundOneShot,
  forge::AnimEventType::SFXSpawn,
  forge::AnimEventType::ComboWindow,
  forge::AnimEventType::ParryWindow,
  forge::AnimEventType::DeflectWindow,
  forge::AnimEventType::PerilousMark,
  forge::AnimEventType::RootMotionBegin,
  forge::AnimEventType::RootMotionEnd,
  forge::AnimEventType::RootMotionScale,
  forge::AnimEventType::SetKinematic,
  forge::AnimEventType::RestorePhysics,
  forge::AnimEventType::LockInput,
  forge::AnimEventType::UnlockInput,
  forge::AnimEventType::HyperArmorBegin,
  forge::AnimEventType::HyperArmorEnd,
  forge::AnimEventType::ScriptedSequence,
  forge::AnimEventType::StartTrail,
  forge::AnimEventType::StopTrail,
  forge::AnimEventType::SpawnImpactVFX
};
static constexpr int k_eventTypeCount = (int)(sizeof(k_allEventTypes) / sizeof(k_allEventTypes[0]));

const char* TaeEditorUI::eventTypeName(forge::AnimEventType t) {
  switch (t) {
    case forge::AnimEventType::SpawnHitbox: return "SpawnHitbox";
    case forge::AnimEventType::IFrame: return "IFrame";
    case forge::AnimEventType::SoundOneShot: return "SoundOneShot";
    case forge::AnimEventType::SFXSpawn: return "SFXSpawn";
    case forge::AnimEventType::ComboWindow: return "ComboWindow";
    case forge::AnimEventType::ParryWindow: return "ParryWindow";
    case forge::AnimEventType::DeflectWindow: return "DeflectWindow";
    case forge::AnimEventType::PerilousMark: return "PerilousMark";
    case forge::AnimEventType::RootMotionBegin: return "RootMotionBegin";
    case forge::AnimEventType::RootMotionEnd: return "RootMotionEnd";
    case forge::AnimEventType::RootMotionScale: return "RootMotionScale";
    case forge::AnimEventType::SetKinematic: return "SetKinematic";
    case forge::AnimEventType::RestorePhysics: return "RestorePhysics";
    case forge::AnimEventType::LockInput: return "LockInput";
    case forge::AnimEventType::UnlockInput: return "UnlockInput";
    case forge::AnimEventType::HyperArmorBegin: return "HyperArmorBegin";
    case forge::AnimEventType::HyperArmorEnd: return "HyperArmorEnd";
    case forge::AnimEventType::ScriptedSequence: return "ScriptedSequence";
    case forge::AnimEventType::StartTrail: return "StartTrail";
    case forge::AnimEventType::StopTrail: return "StopTrail";
    case forge::AnimEventType::SpawnImpactVFX: return "SpawnImpactVFX";
    default: return "Unknown";
  }
}

unsigned int TaeEditorUI::eventColor(forge::AnimEventType t) {
  switch (t) {
    case forge::AnimEventType::SpawnHitbox: return IM_COL32(255, 120, 40, 210); // orange
    case forge::AnimEventType::IFrame: return IM_COL32(60, 120, 255, 210); // blue
    case forge::AnimEventType::ComboWindow: return IM_COL32(60, 200, 80, 210); // green
    case forge::AnimEventType::ParryWindow: return IM_COL32(60, 220, 220, 210); //cyan
    case forge::AnimEventType::DeflectWindow: return IM_COL32(60, 180, 255, 210); // sky blue
    case forge::AnimEventType::PerilousMark: return IM_COL32(220, 40, 40, 210); // red
    case forge::AnimEventType::HyperArmorBegin:
    case forge::AnimEventType::HyperArmorEnd: return IM_COL32(180, 80, 220, 210); // purple
    case forge::AnimEventType::LockInput:
    case forge::AnimEventType::UnlockInput: return IM_COL32(200, 200, 40, 210); // yellow
    case forge::AnimEventType::SoundOneShot: return IM_COL32(220, 220, 220, 210); // white
    case forge::AnimEventType::RootMotionBegin:
    case forge::AnimEventType::RootMotionEnd:
    case forge::AnimEventType::RootMotionScale: return IM_COL32(40, 200, 180, 210); //teal
    case forge::AnimEventType::SetKinematic:
    case forge::AnimEventType::RestorePhysics: return IM_COL32(160, 160, 160, 210); //grey
    default: return IM_COL32(140, 140, 140, 210); // dark grey
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
    default:
      return false;
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
    default:
      return false;
  }
}

TaeEditorUI::TaeEditorUI(TaeEditorApp& app) : m_app(app)
{}

void TaeEditorUI::draw() {
  drawMenuBar();
  drawOpenDialog();
  drawOpenMovesetDialog();

  if (m_app.isLoaded()) {
    drawClipInfo();
    drawTimeline();
    drawEventList();
  } else {
    // Nothing is loaded
    ImGuiIO io = ImGui::GetIO();
    ImGui::SetNextWindowPos({ io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Always, { 0.5f, 0.5f });
    ImGui::SetNextWindowSize({ 560.0f, 110.0f }, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("##welcome", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove);
    ImGui::TextDisabled("No clip loaded.");
    ImGui::Spacing();
    ImGui::Text("File > Open Moveset Action (combat animations with TAE Events)");
    ImGui::Text("File > Open clip (locomotion / per-animation TAE)");
    ImGui::End();
  }
}

void TaeEditorUI::drawMenuBar() {
  if (!ImGui::BeginMainMenuBar()) return;

  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("Open Moveset Action..."))
      m_showMovesetDialog = true;
    if (ImGui::MenuItem("Open clip..."))
      m_showOpenDialog = true;

    ImGui::Separator();

    bool hasClip = m_app.isLoaded();
    if (ImGui::MenuItem("Save Sidecar", "Ctrl+S", false, hasClip))
      m_app.saveToSidecar();

    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("View")) {
    ImGui::MenuItem("Clip Info", nullptr, nullptr);
    ImGui::MenuItem("Timeline", nullptr, nullptr);
    ImGui::MenuItem("Event List", nullptr, nullptr);
    ImGui::EndMenu();
  }

  // Show loaded paths in menu bar right hand side
  if (m_app.isLoaded()) {
    float available = ImGui::GetContentRegionAvail().x;
    std::string info = m_app.clipPath();
    float textW = ImGui::CalcTextSize(info.c_str()).x;
    if (textW + 16.0f < available) {
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available - textW - 8.0f);
      ImGui::TextDisabled("%s", info.c_str());
    }
  }

  if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S) && m_app.isLoaded())
    m_app.saveToSidecar();

  ImGui::EndMainMenuBar();
}

void TaeEditorUI::drawOpenDialog() {
  if (!m_showOpenDialog) return;
  ImGui::SetNextWindowSize({ 560.0f, 160.0f }, ImGuiCond_Always);
  ImGui::SetNextWindowPos(
    { ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f },
    ImGuiCond_Always, { 0.5f, 0.5f });

  if (!ImGui::Begin("Open Clip", &m_showOpenDialog,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
    ImGui::End();
    return;
  }

  ImGui::Text("Character GLB (relative to asset root):");
  ImGui::SetNextItemWidth(-1.0f);
  bool loadChar = ImGui::InputText("##charpath", m_charPathBuff, sizeof(m_charPathBuff),
                                   ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::Spacing();
  ImGui::Text("Animation GLB (relative to asset root):");
  ImGui::SetNextItemWidth(-1.0f);
  bool loadAnim = ImGui::InputText("##clippath", m_clipPathBuff, sizeof(m_clipPathBuff),
                                   ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::Spacing();
  bool clickLoad = ImGui::Button("Load", { 80.0f, 0.0f });
  ImGui::SameLine();
  if (ImGui::Button("Cancel", { 80.0f, 0.0f }))
    m_showOpenDialog = false;

  if (clickLoad || (loadChar && m_clipPathBuff[0] != '\0') || loadAnim) {
    if (m_charPathBuff[0] != '\0' && m_clipPathBuff[0] != '\0') {
      m_app.loadClipAndSkeleton(m_charPathBuff, m_clipPathBuff);
      m_showOpenDialog = false;
    }
  }

  ImGui::End();
}

void TaeEditorUI::drawOpenMovesetDialog() {
  if (!m_showMovesetDialog) return;

  // Retrieve all loaded moveset defs, sorted by IDs for stable combo ordering
  const auto& allMovesets = forge::AssetManager::getAllMovesetDefs();

  std::vector<const forge::MovesetDef*> sortedMovesets;
  sortedMovesets.reserve(allMovesets.size());
  for (const auto& [id, def] : allMovesets)
    sortedMovesets.push_back(&def);
  std::sort(sortedMovesets.begin(), sortedMovesets.end(),
            [](const forge::MovesetDef* a, const forge::MovesetDef* b) {
              return a->id < b->id;
            });

  ImGui::SetNextWindowSize({ 560.0f, 200.0f }, ImGuiCond_Always);
  ImGui::SetNextWindowPos({ ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f },
                          ImGuiCond_Always, { 0.5f, 0.5f });

  if (!ImGui::Begin("Open Moveset Action", &m_showMovesetDialog,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
    ImGui::End();
    return;
  }

  if (m_charPathBuff[0] == '\0' && !m_app.charPath().empty())
    strncpy(m_charPathBuff, m_app.charPath().c_str(), sizeof(m_charPathBuff) - 1);

  ImGui::Text("Character GLB (relative to asset root):");
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputText("##charpath_mv", m_charPathBuff, sizeof(m_charPathBuff));

  ImGui::Spacing();

  if (sortedMovesets.empty()) {
    ImGui::TextColored({ 1.0f, 0.4f, 0.4f, 1.0f },
                       "No movesets loaded, Check assets/data/movesets.json");
    if (ImGui::Button("Cancel", { 80.0f, 0.0f }))
      m_showMovesetDialog = false;
    ImGui::End();
    return;
  }

  m_selectedMovesetIdx = std::clamp(m_selectedMovesetIdx, 0, (int)sortedMovesets.size() - 1);

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
          m_selectedActionIdx = 0;
          selectedMoveset = sortedMovesets[i];
        }
      }
      if (isSel)
        ImGui::SetItemDefaultFocus();
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
        if (isSel)
          ImGui::SetItemDefaultFocus();
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
    m_app.loadMovesetAction(
      m_charPathBuff,
      selectedMoveset->id,
      actionKeys[m_selectedActionIdx]);
    m_showMovesetDialog = false;
  }

  ImGui::End();
}

void TaeEditorUI::drawClipInfo() {
  ImGui::SetNextWindowPos({ 8.0f, 26.0f }, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({ 320.0f, 0.0f }, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.88f);

  if (!ImGui::Begin("Clip Info", nullptr,
                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  forge::AnimationClip* clip = m_app.clip();

  // Clip name and duration
  if (!m_app.activeActionKey().empty()) {
    ImGui::TextColored({ 0.9f, 0.75f, 0.3f, 1.0f },
                       "%s", m_app.activeMovesetId().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(" \xe2\x86\x92 "); // UTF8 -> arrow
    ImGui::SameLine();
    ImGui::TextColored({ 0.6f, 0.9f, 0.5f, 1.0f },
                       "%s", m_app.activeActionKey().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("  %.3fs", clip->duration);
  } else {
    ImGui::TextColored({ 0.9f, 0.75f, 0.3f, 1.0f }, "%s", clip->name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("  %.3fs", clip->duration);
  }
  // Play/Pause btn
  const char* playLabel = m_app.isPlaying() ? "  ||  Pause" : "  >  Play ";
  if (ImGui::Button(playLabel))
    m_app.togglePlaying();

  ImGui::SameLine();

  // Scrub time as draggable number
  float t = m_app.scrubTime();
  ImGui::SetNextItemWidth(90.0f);
  if (ImGui::DragFloat("##scrubval", &t, 0.001f, 0.0f, clip->duration, "%.3f s"))
    m_app.setScrubTime(t);

  ImGui::SameLine();
  ImGui::TextDisabled("/ %.3f s", clip->duration);

  // Progress bar spanning full panel width
  float progress = (clip->duration > 0.0f) ? m_app.scrubTime() / clip->duration : 0.0f;
  ImGui::ProgressBar(progress, { -1.0f, 4.0f }, "");

  ImGui::Spacing();

  // Event count
  ImGui::TextDisabled("%d events  sidecar: %s",
                      (int)m_app.events().size(),
                      m_app.clipPath().empty() ? "-" : m_app.clipPath().c_str());

  ImGui::End();
}

void TaeEditorUI::drawTimeline() {
  ImGuiIO& io = ImGui::GetIO();
  forge::AnimationClip* clip = m_app.clip();
  if (!clip || clip->duration <= 0.0f) return;

  const float MENU_BAR_H = ImGui::GetFrameHeight();
  const float PANEL_H = 200.0f;
  const float LABEL_W = 148.0f;
  const float RULER_H = 24.0f;
  const float TRACK_H = 20.0f;
  const float GRAB_W = 5.0f;
  const float TICK_RADIUS = 5.0f; //scrub handle

  // Pin to btm of screen, full width
  ImGui::SetNextWindowPos({ 0.0f, io.DisplaySize.y - PANEL_H }, ImGuiCond_Always);
  ImGui::SetNextWindowSize({ io.DisplaySize.x, PANEL_H }, ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.93f);

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
                         | ImGuiWindowFlags_NoResize
                         | ImGuiWindowFlags_NoMove
                         | ImGuiWindowFlags_NoScrollbar
                         | ImGuiWindowFlags_NoScrollWithMouse
                         | ImGuiWindowFlags_NoBringToFrontOnFocus;

  if (!ImGui::Begin("##timeline", nullptr, flags)) {
    ImGui::End();
    return;
  }

  // toolbar
  const char* playLabel = m_app.isPlaying() ? "|| Pause" : " > Play ";
  if (ImGui::Button(playLabel))
    m_app.togglePlaying();

  ImGui::SameLine();

  ImGui::Text("%.3f / %.3f s", m_app.scrubTime(), clip->duration);

  ImGui::SameLine(0.0f, 20.0f);

  if (ImGui::Button("Add Event")) {
    forge::AnimEvent ev;
    ev.type = forge::AnimEventType::SpawnHitbox;
    ev.startTime = m_app.scrubTime();
    ev.endTime = std::min(m_app.scrubTime() + 0.1f, clip->duration);
    ev.hitboxExtents = { 0.3f, 0.8f, 0.15f };
    m_app.events().push_back(ev);
    m_app.selectedEvent() = (int)m_app.events().size() - 1;
  }

  ImGui::SameLine();

  ImGui::BeginDisabled(m_app.selectedEvent() < 0 ||
                       m_app.selectedEvent() >= (int)m_app.events().size());
  if (ImGui::Button("Delete")) {
    auto& evs = m_app.events();
    evs.erase(evs.begin() + m_app.selectedEvent());
    m_app.selectedEvent() = -1;
  }
  ImGui::EndDisabled();

  ImGui::SameLine(0.0f, 20.0f);

  if (ImGui::Button("Save (Ctrl+S)"))
    m_app.saveToSidecar();
  ImGui::Separator();

  // Draw area setup
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 cp = ImGui::GetCursorScreenPos();
  float contW = ImGui::GetContentRegionAvail().x;
  float trackW = contW - LABEL_W;

  if (trackW < 1.0f) { ImGui::End(); return; }

  float pxPerSec = trackW / clip->duration;

  auto timeToX = [&](float t) -> float {
    return cp.x + LABEL_W + t * pxPerSec;
  };
  auto xToTime = [&](float x) -> float {
    return std::clamp((x - cp.x - LABEL_W) / pxPerSec, 0.0f, clip->duration);
  };

  // ruler background
  dl->AddRectFilled(cp, { cp.x + contW, cp.y + RULER_H }, IM_COL32(28, 28, 32, 255));

  // Compute tick interval
  float targetTicks = 8.0f;
  float rawInterval = clip->duration / targetTicks;
  float mag = std::pow(10.0f, std::floor(std::log10(rawInterval)));
  float norm = rawInterval / mag;
  float niceFactor = (norm < 1.5f) ? 1.0f : (norm < 3.5f) ? 2.0f : 5.0f;
  float tickInterval = niceFactor * mag;

  // Major ticks + time labels, minor ticks in between
  for (float t = 0.0f; t <= clip->duration + 0.0001f; t += tickInterval / 5.0f) {
    float x = timeToX(t);
    bool major = (std::fmod(t + 1e-6f, tickInterval) < tickInterval / 5.0f * 0.5f);
    float lineTop = major ? cp.y + 2.0f : cp.y + RULER_H * 0.55f;
    dl->AddLine({ x, lineTop }, { x, cp.y + RULER_H },
                major ? IM_COL32(180, 180, 180, 220)
                      : IM_COL32(90, 90, 90, 180));

    if (major) {
      char buff[16];
      snprintf(buff, sizeof(buff), "%.2f", t);
      dl->AddText({ x + 3.0f, cp.y + 3.0f }, IM_COL32(180, 180, 180, 255), buff);
    }
  }

  // Right boundary line
  float endX = timeToX(clip->duration);
  dl->AddLine({ endX, cp.y }, { endX, cp.y + RULER_H }, IM_COL32(200, 80, 80, 200), 1.5f);

  float rulerBottom = cp.y + RULER_H;

  // Event rows 
  auto& events = m_app.events();
  int& selIdx = m_app.selectedEvent();

  for (int i = 0; i < (int)events.size(); i++) {
    forge::AnimEvent& ev = events[i];
    float rowTop = rulerBottom + i * TRACK_H;
    float rowBottom = rowTop + TRACK_H;
    float rowMid = rowTop + TRACK_H * 0.5f;

    // Row background
    bool selected = (selIdx == i);
    ImU32 rowBg = selected
      ? IM_COL32(55, 75, 115, 200)
      : (i % 2 == 0 ? IM_COL32(28, 28, 32, 200) : IM_COL32(34, 34, 40, 200));
    dl->AddRectFilled({ cp.x, rowTop }, { cp.x + contW, rowBottom }, rowBg);

    // Type label -> left col
    // Tint label text with the event color (dimmed)
    ImU32 labelCol = IM_COL32(210, 210, 210, 255);
    dl->AddText({ cp.x + 6.0f, rowTop + (TRACK_H - 13.0f) * 0.5f },
                labelCol, eventTypeName(ev.type));

    // Compute bar pixel range
    bool oneShot = (ev.startTime == ev.endTime);
    float x0 = timeToX(ev.startTime);
    float x1 = oneShot ? x0 + 4.0f : timeToX(ev.endTime);
    float barTop = rowTop + 3.0f;
    float barBottom = rowBottom - 3.0f;
    ImU32 barCol = eventColor(ev.type);

    // Draw event bar or tick
    if (oneShot) {
      // Vertical tick: narrow filled rect + small circle on top
      dl->AddRectFilled({ x0 - 2.0f, barTop }, { x0 + 2.0f, barBottom }, barCol);
      dl->AddCircleFilled({ x0, barTop }, 3.5f, barCol);
    } else {
      dl->AddRectFilled({ x0, barTop }, { x1, barBottom }, barCol, 2.0f);
      if (selected)
        dl->AddRect({ x0 - 1.0f, barTop - 1.0f }, { x1 + 1.0f, barBottom + 1.0f },
                    IM_COL32(255, 255, 255, 220), 2.0f, 0, 1.5f);
    }

    ImGui::PushID(i);

    float barW = x1 - x0;

    if (oneShot) {
      ImGui::SetCursorScreenPos({ x0 - GRAB_W * 2.0f, rowTop });
      ImGui::InvisibleButton("##os", { GRAB_W * 4.0f, TRACK_H });
      if (ImGui::IsItemClicked()) {
        selIdx = i;
        m_app.setScrubTime(ev.startTime);
      }
      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        float newT = std::clamp(ev.startTime + io.MouseDelta.x / pxPerSec,
                                0.0f, clip->duration);
        ev.startTime = newT;
        ev.endTime = newT;
      }
    } else {
      // Duration event: three zones - left edge, center, right edge

      // Left edge (resize start time)
      ImGui::SetCursorScreenPos({ x0, rowTop });
      ImGui::InvisibleButton("##le", { GRAB_W, TRACK_H });
      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ev.startTime = std::clamp(ev.startTime + io.MouseDelta.x / pxPerSec,
                                  0.0f, ev.endTime - 0.005f);
      }
      if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

      // Center (select + move whole event)
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
          float len = ev.endTime - ev.startTime;
          float delta = io.MouseDelta.x / pxPerSec;
          ev.startTime = std::clamp(ev.startTime + delta, 0.0f, clip->duration - len);
          ev.endTime = ev.startTime + len;
        }
      }

      // Right edge (resize endTime)
      ImGui::SetCursorScreenPos({ x1 - GRAB_W, rowTop });
      ImGui::InvisibleButton("##re", { GRAB_W, TRACK_H });
      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ev.endTime = std::clamp(ev.endTime + io.MouseDelta.x / pxPerSec,
                                ev.startTime + 0.005f, clip->duration);
      }
      if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    ImGui::PopID();
  }

  // Scrub line
  float scrubX = timeToX(m_app.scrubTime());
  float trackBottom = rulerBottom + (float)events.size() * TRACK_H;
  dl->AddLine({ scrubX, rulerBottom }, { scrubX, trackBottom },
              IM_COL32(255, 210, 50, 230), 1.5f);
  dl->AddCircleFilled({ scrubX, rulerBottom + TICK_RADIUS }, TICK_RADIUS, IM_COL32(255, 210, 50, 255));

  // Ruler click/drag to set scrub time
  // Invisible btn must sit exactly over the ruler strip
  // Set the cursor AFTER all event rows so we don't obscure them
  ImGui::SetCursorScreenPos({ cp.x + LABEL_W, cp.y });
  ImGui::InvisibleButton("##ruler", { trackW, RULER_H });
  if (ImGui::IsItemActive())
    m_app.setScrubTime(xToTime(ImGui::GetMousePos().x));

  // Advance internal cursor past all rows so the window doesn't clip
  // content
  ImGui::SetCursorScreenPos({ cp.x, trackBottom });
  ImGui::Dummy({ 0.0f, 4.0f });

  ImGui::End();
}

void TaeEditorUI::drawEventList() {
  ImGui::SetNextWindowPos({ 8.0f, 130.0f }, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({ 560.0f, 360.0f }, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.92f);

  if (!ImGui::Begin("Events", nullptr, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  forge::AnimationClip* clip = m_app.clip();
  auto& events = m_app.events();
  int& selIdx = m_app.selectedEvent();

  // Table
  // Six cols: Type | Start | End | Payload | Extents | x
  ImGuiTableFlags tflags = ImGuiTableFlags_Borders
                         | ImGuiTableFlags_RowBg
                         | ImGuiTableFlags_ScrollY
                         | ImGuiTableFlags_SizingFixedFit;

  float tableH = ImGui::GetContentRegionAvail().y - 36.0f;

  if (ImGui::BeginTable("##evtable", 6, tflags, { 0.0f, tableH })) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 140.0f);
    ImGui::TableSetupColumn("Start (s)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("End (s)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Payload", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Extents", ImGuiTableColumnFlags_WidthFixed, 160.0f);
    ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, 20.0f);
    ImGui::TableHeadersRow();

    for (int i = 0; i < (int)events.size(); i++) {
      forge::AnimEvent& ev = events[i];
      bool selected = (selIdx == i);

      ImGui::PushID(i);
      ImGui::TableNextRow();

      // highlight selected row
      if (selected)
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                               IM_COL32(55, 75, 115, 160));

      // Col 0: Type dropdown
      ImGui::TableSetColumnIndex(0);
      ImGui::SetNextItemWidth(-1.0f);
      if (ImGui::BeginCombo("##type", eventTypeName(ev.type))) {
        for (int t = 0; t < k_eventTypeCount; t++) {
          bool isSel = (ev.type == k_allEventTypes[t]);
          if (ImGui::Selectable(eventTypeName(k_allEventTypes[t]), isSel))
            ev.type = k_allEventTypes[t];
          if (isSel)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      // Col 1: Start time
      ImGui::TableSetColumnIndex(1);
      ImGui::SetNextItemWidth(-1.0f);
      bool oneShotType = isOneShot(ev.type);
      if (ImGui::DragFloat("##st", &ev.startTime, 0.001f, 0.0f,
                           oneShotType ? clip->duration : ev.endTime - 0.001f,
                           "%.3f")) {
        if (oneShotType)
          ev.endTime = ev.startTime;
        selIdx = i;
        m_app.setScrubTime(ev.startTime);
      }

      // Col 2: End Time
      ImGui::TableSetColumnIndex(2);
      ImGui::SetNextItemWidth(-1.0f);
      if (oneShotType) {
        ImGui::BeginDisabled(true);
        ImGui::DragFloat("##et_dis", &ev.endTime, 0.001f, 0.0f, clip->duration, "-");
        ImGui::EndDisabled();
      } else {
        if (ImGui::DragFloat("##et", &ev.endTime, 0.001f, ev.startTime + 0.001f, clip->duration, "%.3f")) {
          selIdx = i;
        }
      }

      // Col 3: Payload
      ImGui::TableSetColumnIndex(3);
      ImGui::SetNextItemWidth(-1.0f);
      
      if (usesBonePayload(ev.type)) {
        // Dropdown of known weapon bone names
        bool valid = !ev.payload.empty() &&
                    std::find(m_app.knownBones().begin(), m_app.knownBones().end(),
                              ev.payload) != m_app.knownBones().end();
        bool pushedColor = !valid && !ev.payload.empty();
        if (pushedColor)
          ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(140, 40, 40, 120));
        if (ImGui::BeginCombo("##paybonecombo", ev.payload.empty() ? "-" : ev.payload.c_str())) {
          for (const auto& bone : m_app.knownBones())
            if (ImGui::Selectable(bone.c_str(), ev.payload == bone))
              ev.payload = bone;
          ImGui::EndCombo();
        }
        if (pushedColor)
          ImGui::PopStyleColor();
      } else if (usesComboPayload(ev.type)) {
        // Dropdown of known moveset clip names
        bool valid = !ev.payload.empty() &&
                    std::find(m_app.knownComboKeys().begin(), m_app.knownComboKeys().end(),
                              ev.payload) != m_app.knownComboKeys().end();
        bool pushedColor = !valid && !ev.payload.empty();
        if (pushedColor)
          ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(140, 40, 40, 120));
        if (ImGui::BeginCombo("##paycombocombo", ev.payload.empty() ? "-" : ev.payload.c_str())) {
          for (const auto& key : m_app.knownComboKeys())
            if (ImGui::Selectable(key.c_str(), ev.payload == key))
              ev.payload = key;
          ImGui::EndCombo();
        }
        if (pushedColor)
          ImGui::PopStyleColor();
      } else if (usesTextPayload(ev.type)) {
        char buff[256] = {};
        strncpy(buff, ev.payload.c_str(), sizeof(buff) - 1);
        if (ImGui::InputText("##paytxt", buff, sizeof(buff)))
          ev.payload = buff;
      } else {
        // No Payload
        ImGui::BeginDisabled(true);
        ImGui::TextUnformatted("-");
        ImGui::EndDisabled();
      }

      // Col 4: Hitbox extents
      ImGui::TableSetColumnIndex(4);
      ImGui::SetNextItemWidth(-1.0f);
      if (usesHitboxExtents(ev.type)) {
        ImGui::DragFloat3("##ext", &ev.hitboxExtents.x,
                         0.005f, 0.01f, 5.0f, "%.3f");
      } else {
        ImGui::BeginDisabled(true);
        ImGui::TextUnformatted("-");
        ImGui::EndDisabled();
      }

      // Col 5: Delete Button
      ImGui::TableSetColumnIndex(5);
      if (ImGui::SmallButton("×")) {
        events.erase(events.begin() + i);
        if (selIdx >= (int)events.size())
          selIdx = (int)events.size() - 1;
        ImGui::PopID();
        break;
      }

      if (ImGui::IsItemActivated() && !ImGui::IsItemDeactivatedAfterEdit()){
        // Fallthrough, smallbutton already handles click
      }

      // Select on row hover + click
      // Use tablenextrow-level check
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
    ev.type = forge::AnimEventType::SpawnHitbox;
    ev.startTime = m_app.scrubTime();
    ev.endTime = std::min(m_app.scrubTime() + 0.1f, clip->duration);
    ev.hitboxExtents = { 0.3f, 0.8f, 0.15f };
    events.push_back(ev);
    selIdx = (int)events.size() - 1;
  }
  ImGui::SameLine();
  if (ImGui::Button("Save Sidecar"))
    m_app.saveToSidecar();

  ImGui::End();
}
