#pragma once

#include <deque>
#include <string>
#include <vector>

struct GLFWwindow;

namespace forge {

class CombatSystem;
class FlagManager;
class LuaState;
class AIComponent;
class Animator;

// DebugUI - owns the ImGUI lifecycle so we keep Application.cpp clean
//
// Lifecycle (called by Application)
//  init(window) -> after openGL context is ready
//  begin(dt) -> top of each render frame (before onRender)
//  end() -> bottom of each render frame (after onRender before SwapBuffers)
//  shutdown() -> Before GLFW teardown
//
// Panels:
//  [F1] Performance - FPS, frame time, total time
//  [F2] CombatManager - HP/STA/poise bars + one-click hit button per combatant
//  [F3] Flag Browser - table of set flags with clear buttons + manual set
//  [F4] Lua Console - REPL: execute arbitrary Lua, output captured from print()
//  [F5] AI Inspector - A per-AIComponent state + distance bars
//  [F6] AnimGraph Monitor - current clip, playback time, bone count (Phase 9 stub)


class DebugUI {
public:
  DebugUI() = default;
  ~DebugUI() = default;

  // Cant copy, owns ImGUI context
  DebugUI(const DebugUI&) = delete;
  DebugUI& operator=(const DebugUI&) = delete;

  void init(GLFWwindow* window);
  void begin(float dt); // Builds all ImGUI panels for this frame
  void end(); // Renders ImGUI draw data to screen
  void shutdown();

  // True when cursor is over ImGUI panel - caller should suppress game input
  bool isCapturingMouse() const;

  void setCombatSystem(CombatSystem* combat) { m_combat = combat; }
  void setFlagManager(FlagManager* flags) { m_flags = flags; }

  // wires the lua vm and overrides print() so all output routes to the console panel
  // must be called after init() and before the first begin()
  void setLuaState(LuaState* lua);

  // Registers AIComponent for the inspector panel
  // Call once per AI Entity in games onInit after the AI component is created
  void registerAIComponent(AIComponent* ai) { if (ai) m_aiComponents.push_back(ai); }

  void registerAnimator(Animator* anim, const std::string& label) {
    if (anim) m_animators.push_back({ anim, label });
  }

  // Appends a line to console panel output buffer
  void addConsoleLine(std::string line);
private:
  void drawPerformancePanel();
  void drawCombatMonitorPanel();
  void drawFlagBrowserPanel();
  void drawLuaConsolePanel();
  void drawAIInspectorPanel();
  void drawAnimGraphMonitorPanel();

  CombatSystem* m_combat = nullptr;
  FlagManager* m_flags = nullptr;
  LuaState* m_lua = nullptr;
  std::vector<AIComponent*> m_aiComponents;

  struct AnimatorEntry {
    Animator* animator;
    std::string label;
  };
  std::vector<AnimatorEntry> m_animators;

  // Panel visibility
  bool m_showPerformance = true;
  bool m_showCombatMonitor = true;
  bool m_showFlagBrowser = false;
  bool m_showLuaConsole = false;
  bool m_showAIInspector = false;
  bool m_showAnimGraphMonitor = true;

  // Timing state (updatd in begin)
  float m_fps = 0.0f;
  float m_frameTimeMs = 0.0f;
  float m_totalTime = 0.0f;

  // Lua Console state
  static constexpr int k_inputBuffSize = 512;
  static constexpr int k_maxLines = 200;
  char m_inputBuff[k_inputBuffSize] = {};
  std::deque<std::string> m_consoleLines;
  bool m_scrollToBottom = false;

};
}
