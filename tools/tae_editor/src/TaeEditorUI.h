#pragma once
#include <forge/AnimationClip.h>
#include <string>
#include <vector>

class TaeEditorApp;

class TaeEditorUI {
public:
  explicit TaeEditorUI(TaeEditorApp& app);
  void draw();

private:
  TaeEditorApp& m_app;

  // -----------------------------------------------------------------------
  // Dialog visibility flags
  // -----------------------------------------------------------------------
  bool m_showOpenDialog           = false;  // Legacy "Open Clip" (char + anim paths)
  bool m_showMovesetDialog        = false;  // Open moveset action (existing JSON movesets)
  bool m_showModelLoadDialog      = false;  // Load reference model independently
  bool m_showAnimBrowserDialog    = false;  // Browse folder for animations
  bool m_showNewMovesetDialog     = false;  // Create new moveset
  bool m_showLoadMovesetEditDialog = false; // Load existing moveset into editor
  bool m_showSaveMovesetDialog    = false;  // Save moveset to a specific path
  bool m_showMovesetEditorPanel   = true;   // Toggle for side panel

  // -----------------------------------------------------------------------
  // Legacy "Open Clip" dialog state
  // -----------------------------------------------------------------------
  int  m_selectedMovesetIdx = 0;
  int  m_selectedActionIdx  = 0;
  char m_charPathBuff[512]  = {};
  char m_clipPathBuff[512]  = {};

  // -----------------------------------------------------------------------
  // Model load dialog state
  // -----------------------------------------------------------------------
  char m_modelPathBuff[512] = {};

  // -----------------------------------------------------------------------
  // Animation browser state
  // -----------------------------------------------------------------------
  char m_animBrowserDirBuff[512]      = {};
  char m_animBrowserFilterBuff[128]   = {};
  std::vector<std::string> m_animBrowserFiles;        // Relative paths
  std::vector<std::string> m_animBrowserDisplayNames; // Filenames only (for display)
  int  m_animBrowserSelectedIdx       = -1;
  bool m_animBrowserAddToMoveset      = false; // true → selection adds to moveset

  // When adding to moveset: action key to assign
  char m_pendingActionKeyBuff[128] = {};

  // -----------------------------------------------------------------------
  // New moveset dialog state
  // -----------------------------------------------------------------------
  char m_newMovesetIdBuff[128]          = {};
  char m_newMovesetOutputPathBuff[512]  = {};

  // -----------------------------------------------------------------------
  // Load-for-edit dialog state
  // -----------------------------------------------------------------------
  int m_loadMovesetEditIdx = 0;

  // -----------------------------------------------------------------------
  // Save moveset dialog state
  // -----------------------------------------------------------------------
  char m_saveMovesetPathBuff[512] = {};

  // -----------------------------------------------------------------------
  // Draw methods — 1:1 with ImGui window or panel
  // -----------------------------------------------------------------------
  void drawMenuBar();
  void drawWelcomeScreen();

  // Clip loading dialogs
  void drawOpenDialog();           // Legacy combined char + clip path dialog
  void drawOpenMovesetDialog();    // Load action from registered moveset
  void drawModelLoadDialog();      // Load reference model independently

  // Animation browser
  void drawAnimBrowserDialog();

  // Moveset editor
  void drawMovesetEditorPanel();
  void drawNewMovesetDialog();
  void drawLoadMovesetEditDialog();
  void drawSaveMovesetDialog();

  // Clip info and TAE panels
  void drawClipInfo();
  void drawTimeline();
  void drawEventList();

  // -----------------------------------------------------------------------
  // Helpers
  // -----------------------------------------------------------------------
  void scanAnimFolder(const std::string& dirPath);
  void applyAnimBrowserSelection(const std::string& relPath);

  // Per-type metadata used by timeline and event list
  static const char* eventTypeName(forge::AnimEventType t);
  static unsigned int eventColor(forge::AnimEventType t);
  static bool isOneShot(forge::AnimEventType t);
  static bool usesHitboxExtents(forge::AnimEventType t);
  static bool usesBonePayload(forge::AnimEventType t);
  static bool usesComboPayload(forge::AnimEventType t);
  static bool usesTextPayload(forge::AnimEventType t);
};
