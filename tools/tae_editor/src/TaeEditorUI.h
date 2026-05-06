#pragma once
#include <forge/AnimationClip.h>
#include <string>

class TaeEditorApp;

class TaeEditorUI {
public:
  explicit TaeEditorUI(TaeEditorApp& app);

  void draw();

private:
  TaeEditorApp& m_app;

  // Open clip dialog visibility
  bool m_showOpenDialog = false;
  bool m_showMovesetDialog = false;
  int m_selectedMovesetIdx = 0;
  int m_selectedActionIdx = 0;

  // Input buffs for the open dialog
  char m_charPathBuff[512] = {};
  char m_clipPathBuff[512] = {};

  // Draw methods - 1:1 with ImGUI window or panel
  void drawMenuBar();
  void drawOpenDialog();
  void drawOpenMovesetDialog();
  void drawClipInfo();
  void drawTimeline();
  void drawEventList();

  // per-type metadata used by timeline and event list
  static const char* eventTypeName(forge::AnimEventType t);
  static unsigned int eventColor(forge::AnimEventType t);
  static bool isOneShot(forge::AnimEventType t);
  static bool usesHitboxExtents(forge::AnimEventType t);
  static bool usesBonePayload(forge::AnimEventType t);
  static bool usesComboPayload(forge::AnimEventType t);
  static bool usesTextPayload(forge::AnimEventType t);
};
