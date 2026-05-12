#include <forge/DebugUI.h>
#include <forge/CombatSystem.h>
#include <forge/CombatComponent.h>
#include <forge/FlagManager.h>
#include <forge/LuaState.h>
#include <forge/AIComponent.h>
#include <forge/Logger.h>
#include <forge/Animator.h>
#include <forge/LightEnvironment.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <algorithm> // std::clamp

namespace forge {

static int luaConsoleInputCallback(ImGuiInputTextCallbackData* data) {
  DebugUI* ui = static_cast<DebugUI*>(data->UserData);

  if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
    const int prevPos = ui->m_historyPos;

    if (data->EventKey == ImGuiKey_UpArrow) {
      if (ui->m_historyPos == -1)
        ui->m_historyPos = static_cast<int>(ui->m_history.size()) - 1;
      else if (ui->m_historyPos > 0)
        ui->m_historyPos--;
    } else if (data->EventKey == ImGuiKey_DownArrow) {
      if (ui->m_historyPos != -1) {
        ui->m_historyPos++;
        if (ui->m_historyPos >= static_cast<int>(ui->m_history.size()))
          ui->m_historyPos = -1;
      }
    }

    if (ui->m_historyPos != prevPos) {
      // Replace buffer contents with selected history entry (or blank)
      data->DeleteChars(0, data->BufTextLen);
      if (ui->m_historyPos != -1)
        data->InsertChars(0, ui->m_history[ui->m_historyPos].c_str());
    }
  }
  return 0;
}

// Lifecycle

void DebugUI::init(GLFWwindow* window) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGui::StyleColorsDark();

  // install cbs=true -> ImGUI hooks into GLFW for mouse/keyboard itself
  ImGui_ImplGlfw_InitForOpenGL(window, true);
#ifdef __APPLE__
  ImGui_ImplOpenGL3_Init("#version 410");
#else
  ImGui_ImplOpenGL3_Init("#version 460");
#endif

  LOG_INFO("[DebugUI] Initialized (F1=Perf F2=Combat F3=Flags F4=Lua F5=AI, F6=AnimGraph, F7=Physics, F8=Socket, F9=Lights | F9=LuaReload grave=cursor)");
}

void DebugUI::setLuaState(LuaState* lua) {
  m_lua = lua;
  if (!lua) return;

  lua->get().set_function("print", [this](sol::variadic_args args) {
    lua_State* L = args.lua_state();
    std::string line;
    for (auto arg : args) {
      if (!line.empty()) line += '\t';
      luaL_tolstring(L, arg.stack_index(), nullptr);
      line += lua_tostring(L, -1);
      lua_pop(L, 1); // luaL_toLstring pushes onto the stack so we clean that up
    }
    addConsoleLine(std::move(line));
  });

  addConsoleLine("-- Lua console ready. Try: playerCombat:getHp()");
}

void DebugUI::addConsoleLine(std::string line) {
  m_consoleLines.push_back(std::move(line));
  if (static_cast<int>(m_consoleLines.size()) > k_maxLines)
    m_consoleLines.pop_front();
  m_scrollToBottom = true;
}

void DebugUI::begin(float dt) {
  // Update timing
  m_frameTimeMs = dt * 1000.0f;
  constexpr float kAlpha = 0.1f;
  float instantFps = (dt > 1e-6f) ? (1.0f / dt) : 0.0f;
  m_fps = m_fps * (1.0f - kAlpha) + instantFps * kAlpha;
  m_totalTime += dt;

  // New frame
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  // Panel toggles (processed after NewFrame so ImGui owns keystate)
  if (ImGui::IsKeyPressed(ImGuiKey_F1)) m_showPerformance = !m_showPerformance;
  if (ImGui::IsKeyPressed(ImGuiKey_F2)) m_showCombatMonitor = !m_showCombatMonitor;
  if (ImGui::IsKeyPressed(ImGuiKey_F3)) m_showFlagBrowser = !m_showFlagBrowser;
  if (ImGui::IsKeyPressed(ImGuiKey_F4)) m_showLuaConsole = !m_showLuaConsole;
  if (ImGui::IsKeyPressed(ImGuiKey_F5)) m_showAIInspector = !m_showAIInspector;
  if (ImGui::IsKeyPressed(ImGuiKey_F6)) m_showAnimGraphMonitor = !m_showAnimGraphMonitor;
  if (ImGui::IsKeyPressed(ImGuiKey_F7)) m_showPhysicsDebug = !m_showPhysicsDebug;
  if (ImGui::IsKeyPressed(ImGuiKey_F8)) m_showSocketEditor = !m_showSocketEditor;
  if (ImGui::IsKeyPressed(ImGuiKey_F9)) m_showLightEditor = !m_showLightEditor;

  // build panels
  if (m_showPerformance) drawPerformancePanel();
  if (m_showCombatMonitor) drawCombatMonitorPanel();
  if (m_showFlagBrowser) drawFlagBrowserPanel();
  if (m_showLuaConsole) drawLuaConsolePanel();
  if (m_showAIInspector) drawAIInspectorPanel();
  if (m_showAnimGraphMonitor) drawAnimGraphMonitorPanel();
  if (m_showSocketEditor) drawWeaponSocketEditorPanel();
  if (m_showLightEditor) drawLightEditorPanel();
}

void DebugUI::end() {
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void DebugUI::shutdown() {
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  LOG_INFO("[DebugUI] Shut down");
}

bool DebugUI::isCapturingMouse() const {
  return ImGui::GetIO().WantCaptureMouse;
}

// Panels

void DebugUI::drawPerformancePanel() {
  ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(230.0f, 90.0f), ImGuiCond_FirstUseEver);

  if (ImGui::Begin("Performance [F1]")) {
    ImGui::Text("FPS : %6.1f", m_fps);
    ImGui::Text("Frame time : %6.2f ms", m_frameTimeMs);
    ImGui::Text("Total time : %6.1f s", m_totalTime);
  }
  ImGui::End();
}

void DebugUI::drawCombatMonitorPanel() {
  if (!m_combat) return;

  ImGui::SetNextWindowPos(ImVec2(10.0f, 110.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(290.0f, 240.0f), ImGuiCond_FirstUseEver);

  if (ImGui::Begin("Combat monitor [F2]")) {
    const auto& combatants = m_combat->getCombatants();
    
    if (combatants.empty()) {
      ImGui::TextDisabled("No combatants registered");
    } else {
      for (CombatComponent* c : combatants) {
        if (!c) continue;
        ImGui::PushID(c);

        ImGui::SeparatorText(c->getName().c_str());

        // HP Bar
        float hpFrac = (c->getMaxHp() > 0.0f)
          ? std::clamp(c->getHp() / c->getMaxHp(), 0.0f, 1.0f)
          : 0.0f;
        char hpBuff[32];
        snprintf(hpBuff, sizeof(hpBuff), "%.0f / %.0f",
                 c->getHp(), c->getMaxHp());
        ImGui::TextUnformatted("HP "); ImGui::SameLine();
        ImGui::ProgressBar(hpFrac, ImVec2(-1.0f, 0.0f), hpBuff);

        // Stamina bar
        float maxSta = c->getMaxStamina();
        float staFrac = (maxSta > 0.0f)
          ? std::clamp(c->getStamina() / maxSta, 0.0f, 1.0f)
          : 0.0f;
        char staBuff[32];
        snprintf(staBuff, sizeof(staBuff), "%.0f / %.0f",
                 c->getStamina(), maxSta);
        ImGui::TextUnformatted("STA"); ImGui::SameLine();
        ImGui::ProgressBar(staFrac, ImVec2(-1.0f, 0.0f), staBuff);

        // State label
        ImGui::Text("Combat State : %s", c->getStateName().c_str());

        // Test button
        if (ImGui::SmallButton("Hit for 50")) {
          HitEvent ev;
          ev.damage = 50.0f;
          ev.poiseDamage = 10.0f;
          ev.direction = glm::vec3(0.0f, 0.0f, 1.0f);
          ev.damageType = "debug";
          c->applyHit(ev);
        }

        ImGui::PopID();
        ImGui::Spacing();
      }
    }
  }
  ImGui::End();
}

void DebugUI::drawFlagBrowserPanel() {
  if (!m_flags) return;

  ImGui::SetNextWindowPos(ImVec2(10.0f, 360.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(290.0f, 200.0f), ImGuiCond_FirstUseEver);

  if (ImGui::Begin("Flag Browser [F3]")) {
    const auto flagMap = m_flags->getAll();

    // Scrollable table of set flags
    ImGui::BeginTable("##flagtable", 3,
                      ImGuiTableFlags_Borders |
                      ImGuiTableFlags_RowBg |
                      ImGuiTableFlags_ScrollY,
                      ImVec2(0.0f, 130.0f));

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Flag ID", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 45.0f);
    ImGui::TableSetupColumn("##action", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    bool anySet = false;
    for (const auto& [id, val] : flagMap) {
      if (!val) continue;
      anySet = true;

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0); ImGui::Text("%u", id);
      ImGui::TableSetColumnIndex(1);
      ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "true");
      ImGui::TableSetColumnIndex(2);
      ImGui::PushID(static_cast<int>(id));
      if (ImGui::SmallButton("Clear"))
        m_flags->set(id, false);
      ImGui::PopID();
    }

    if (!anySet) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextDisabled("(none)");
    }
    ImGui::EndTable();

    // Manual set
    ImGui::Separator();
    static uint32_t s_manualId = 0;
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputScalar("Flag ID##set", ImGuiDataType_U32, &s_manualId);
    ImGui::SameLine();
    if (ImGui::SmallButton("Set##flag"))
      m_flags->set(s_manualId, true);
  }
  ImGui::End();
}

void DebugUI::drawLuaConsolePanel() {
  ImGui::SetNextWindowPos(ImVec2(10.0f, 570.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(560.0f, 280.0f), ImGuiCond_FirstUseEver);

  if (ImGui::Begin("Lua Console [F4]")) {
    // Output log
    const float footerHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("##scrollregion", ImVec2(0.0f, -footerHeight),
                      false, ImGuiWindowFlags_HorizontalScrollbar);

    for (const auto& line : m_consoleLines) {
      // Color code by prefix so errors/hints stand out
      if (line.rfind("[error]", 0) == 0)
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", line.c_str());
      else if (line.rfind("--", 0) == 0)
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", line.c_str());
      else
        ImGui::TextUnformatted(line.c_str());
    }

    if (m_scrollToBottom) {
      ImGui::SetScrollHereY(1.0f);
      m_scrollToBottom = false;
    }
    ImGui::EndChild();

    // Input bar
    ImGui::Separator();
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Run").x - ImGui::GetStyle().ItemSpacing.x * 2.0f - 6.0f);

    bool execute = ImGui::InputText("##luainput", m_inputBuff, k_inputBuffSize,
                                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory,
                                    luaConsoleInputCallback, this
                                    );
    ImGui::SameLine();
    execute |= ImGui::Button("Run");

    // Keep Keyboard focus on the input after each execution
    if (ImGui::IsItemDeactivated() || execute)
      ImGui::SetKeyboardFocusHere(-1);

    if (execute && m_inputBuff[0] != '\0') {
      if (m_history.empty() || m_history.back() != m_inputBuff)
        m_history.push_back(m_inputBuff);
      m_historyPos = -1;
      //Echo input so the log reads like repl
      addConsoleLine(std::string("> ") + m_inputBuff);

      if (m_lua) {
        auto result = m_lua->get().safe_script(
          m_inputBuff,
          sol::script_pass_on_error
        );

        if (!result.valid()) {
          sol::error err = result;
          addConsoleLine(std::string("[error] ") + err.what());
        } // If script returned value, though, show it
        else if (result.return_count() > 0) {
          sol::object ret = result;
          if (ret.get_type() != sol::type::none)
            addConsoleLine("=> " + ret.as<std::string>());
        }
      } else {
        addConsoleLine("[error] No Lua VM attached.");
      }

      // Clear input and refocus
      m_inputBuff[0] = '\0';
      ImGui::SetKeyboardFocusHere(-1);
    }
  }

  ImGui::End();
}

void DebugUI::drawAIInspectorPanel() {
  ImGui::SetNextWindowPos(ImVec2(310.0f, 10.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(300.0f, 220.0f), ImGuiCond_FirstUseEver);

  if (ImGui::Begin("AI Inspector [F5]")) {
    if (m_aiComponents.empty()) {
      ImGui::TextDisabled("No AI Components registered.");
    } else {
      for (const AIComponent* ai : m_aiComponents) {
        if (!ai) continue;
        ImGui::PushID(ai);
        ImGui::SeparatorText(ai->getName().c_str());

        // State - Color coded so chase/attack jump out visuall
        const std::string& stateName = ai->getStateName();
        ImVec4 stateCol = ImVec4(0.9f, 0.9f, 0.9f, 1.0f); // default white
        if (stateName == "Chase") stateCol = ImVec4(1.0f, 0.8f, 0.1f, 1.0f); // yellow
        else if (stateName == "Attack") stateCol = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); // red
        else if (stateName == "Dead") stateCol = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // Grey
        else if (stateName == "Patrol") stateCol = ImVec4(0.4f, 0.9f, 0.4f, 1.0f); // green
        ImGui::Text("State :"); ImGui::SameLine();
        ImGui::TextColored(stateCol, "%s", stateName.c_str());

        // Distance to player - shown as progress bar against detection radius
        // lets you determine exact proximity at a glance when debugging
        float dist = ai->getDistToPlayer();
        float detRadius = ai->getDetectionRadius();
        float atkRadius = ai->getAttackRadius();

        ImGui::Text("Dist : %.2f m", dist);

        // Detection bar: fills as player approaches detection radius
        float detFrac = std::clamp(1.0f - (dist / detRadius), 0.0f, 1.0f);
        char detBuff[32];
        snprintf(detBuff, sizeof(detBuff), "%.1f / %.1f m", dist, detRadius);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                              ImVec4(0.2f, 0.7f, 1.0f, 1.0f)); // blue
        ImGui::TextUnformatted("Det "); ImGui::SameLine();
        ImGui::ProgressBar(detFrac, ImVec2(-1.0f, 0.0f), detBuff);
        ImGui::PopStyleColor();

        // Attack radius indicator (just a labelled value)
        float enemySpeed = stateName == "Chase"
            ? ai->getChaseSpeed()
            : stateName == "Patrol"
              ? ai->getMoveSpeed()
              : 0.0f;
        ImGui::Text("Atk R : %.1f m | Speed : %.1f",
                    atkRadius, enemySpeed);
         // In-range indicators
        ImGui::Text("Sees player : "); ImGui::SameLine();
        ai->canSeePlayer()
            ? ImGui::TextColored(ImVec4(1,0.4f,0.4f,1), "YES")
            : ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "no");

        ImGui::SameLine(0.0f, 20.0f);
        ImGui::Text("In atk range : "); ImGui::SameLine();
        ai->inAttackRange()
            ? ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "YES")
            : ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "no");

        ImGui::PopID();
        ImGui::Spacing();
      }
    }
  }
  ImGui::End();
}

void DebugUI::drawAnimGraphMonitorPanel() {
  ImGui::SetNextWindowPos(ImVec2(620.0f, 10.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(320.0f, 320.0f), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("AnimGraph Monitor [F6]")) { ImGui::End(); return; }

  if (m_animators.empty()) {
    ImGui::TextDisabled("No animators registered.");
    ImGui::TextDisabled("Call DebugUI::registerAnimator() in onInit.");
    ImGui::End();
    return;
  }

  for (auto& entry : m_animators) {
    Animator* anim = entry.animator;
    if (!anim) continue;

    ImGui::PushID(anim);
    ImGui::SeparatorText(entry.label.c_str());

    // Graph State
    ImGui::TextUnformatted("State "); ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.9f, 1.0f, 1.0f), "%s", anim->getStateInfo().c_str());

    // Active clip progress bar
    float durr = anim->getActiveClipDuration();
    float curTime = anim->getActiveClipTime();
    float frac = (durr > 0.0f) ? std::clamp(curTime / durr, 0.0f, 1.0f) : 0.0f;

    char timeBuff[64];
    snprintf(timeBuff, sizeof(timeBuff), "%.2f / %.2f s", curTime, durr);

    ImVec4 barCol = (durr > 0.0f)
      ? ImVec4(0.2f, 0.8f, 0.3f, 1.0f) // green - playing
      : ImVec4(0.4f, 0.4f, 0.4f, 1.0f); // Grey - not playing

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barCol);
    ImGui::TextUnformatted("Time  "); ImGui::SameLine();
    ImGui::ProgressBar(frac, ImVec2(-1.0f, 0.0f), timeBuff);
    ImGui::PopStyleColor();

    if (anim->isFinished())
      ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f), "  [FINISHED]");

    // Bone count
    ImGui::Text("Bones : %d", (int)anim->getBoneMatrices().size());

    ImGui::Spacing();

    // Param Table
    // Collapsible tree node
    if (ImGui::TreeNodeEx("Params", ImGuiTreeNodeFlags_DefaultOpen)) {
      const AnimParamTable& p = anim->getParams();

      // Floats
      const auto& floats = p.getFloats();
      if (!floats.empty()) {
        ImGui::TextDisabled("  floats");
        for (const auto& [name, val] : floats) {
          ImGui::Text("    %-18s", name.c_str()); ImGui::SameLine();
          ImGui::Text("%.3f", val);
        }
      }

      // Bools
      const auto& bools = p.getBools();
      if (!bools.empty()) {
        ImGui::TextDisabled("  bools");
        for (const auto& [name, val] : bools) {
          ImGui::Text("    %-18s", name.c_str()); ImGui::SameLine();
          val ? ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "true")
              : ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "false");
          
        }
      }

      // Pending triggers
      const auto& triggers = p.getTriggers();
      if (!triggers.empty()) {
        ImGui::TextDisabled("  triggers (pending)");
        for (const auto& name : triggers)
          ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.1f, 1.0f), "    [T] %s", name.c_str());
      }

      if (floats.empty() && bools.empty() && triggers.empty())
        ImGui::TextDisabled("  (no params set)");

      ImGui::TreePop();
    }
    
    ImGui::PopID();
    ImGui::Spacing();
  }

  ImGui::End();
}

void DebugUI::drawWeaponSocketEditorPanel() {
  if (!ImGui::Begin("Weapon Socket Editor")) {
    ImGui::End();
    return;
  }

  const forge::WeaponDef* def = m_equipment
    ? m_equipment->getEquipped(forge::EquipmentComponent::RIGHT_HAND)
    : nullptr;

  if (!def) {
    ImGui::TextDisabled("No weapon equipped.");
    ImGui::End();
    return;
  }

  ImGui::Text("Weapon: %s Bone: %s", def->id.c_str(), def->boneAttach.c_str());
  ImGui::Separator();

  bool changed = false;
  changed |= ImGui::DragFloat3("Position (m)", &m_socketDebug.pos.x, 0.001f);
  changed |= ImGui::DragFloat3("Rotation (deg)", &m_socketDebug.euler.x, 0.25f);
  changed |= ImGui::DragFloat3("Scale", &m_socketDebug.scale.x, 0.01f, 0.01f, 10.0f);

  if (ImGui::Checkbox("Override Active", &m_socketDebug.active))
    changed = true;

  if (changed && m_socketDebug.active) {
    glm::mat4 T = glm::translate(glm::mat4(1.0f), m_socketDebug.pos);
    glm::mat4 R = glm::mat4_cast(glm::quat(glm::radians(m_socketDebug.euler)));
    glm::mat4 S = glm::scale(glm::mat4(1.0f), m_socketDebug.scale);
    m_equipment->setMeshOffsetOverride(forge::EquipmentComponent::RIGHT_HAND, T * R * S);
  }
  if (!m_socketDebug.active) {
    m_equipment->clearMeshOffsetOverride(forge::EquipmentComponent::RIGHT_HAND);
  }

  ImGui::Separator();
  if (ImGui::Button("Log JSON Snippet")) {
    LOG_INFO("[SocketEditor] \"meshOffset\": {{  "
             "\"pos\": [{:.4f},{:.4f},{:.4f}], "
             "\"rot\": [{:.2f},{:.2f},{:.2f}], "
             "\"scale\": [{:.4f},{:.4f},{:.4f}] }}",
             m_socketDebug.pos.x, m_socketDebug.pos.y, m_socketDebug.pos.z,
             m_socketDebug.euler.x, m_socketDebug.euler.y, m_socketDebug.euler.z,
             m_socketDebug.scale.x, m_socketDebug.scale.y, m_socketDebug.scale.z);
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset")) {
    m_socketDebug = {};
    m_equipment->clearMeshOffsetOverride(forge::EquipmentComponent::RIGHT_HAND);
  }

  ImGui::End();
}

void DebugUI::drawLightEditorPanel() {
  if (!m_lights) return;
  if (!ImGui::Begin("Light Editor [F9]")) {
    ImGui::End();
    return;
  }

  ImGui::SeparatorText("Directional Light");
  {
    const Light& d = m_lights->getDirectional();
    float dir[3] = { d.posOrDir.x, d.posOrDir.y, d.posOrDir.z };
    float col[3] = { d.color.x, d.color.y, d.color.z };
    float intensity = d.intensity;

    bool changed = false;
    changed |= ImGui::DragFloat3("Direction##dir", dir, 0.01f, -1.0f, 1.0f);
    changed |= ImGui::ColorEdit3("Color##dircol", col);
    changed |= ImGui::DragFloat("Intensity##dirint", &intensity, 0.01f, 0.0f, 5.0f);

    if (changed) {
      glm::vec3 d3 = glm::normalize(glm::vec3(dir[0], dir[1], dir[2]));
      m_lights->setDirectional(d3, glm::vec3(col[0], col[1], col[2]), intensity);
    }
  }

  ImGui::SeparatorText("Ambient");
  {
    glm::vec3 ac = m_lights->getAmbientColor();
    float ai = m_lights->getAmbientIntensity();
    float col[3] = { ac.x, ac.y, ac.z };

    bool changed = false;
    changed |= ImGui::ColorEdit3("Color##ambcol", col);
    changed |= ImGui::DragFloat("Intensity##ambint", &ai, 0.005f, 0.0f, 1.0f);

    if (changed)
      m_lights->setAmbient(glm::vec3(col[0], col[1], col[2]), ai);
  }

  ImGui::SeparatorText("Point Lights");
  int count = m_lights->getPointLightCount();
  ImGui::Text("Active point lights: %d / %d", count, LightEnvironment::k_maxPointLights);

  for (int i = 0; i < count; ++i) {
    ImGui::PushID(i);
    const Light& pl = m_lights->getPointLight(i);
    float pos[3] = { pl.posOrDir.x, pl.posOrDir.y, pl.posOrDir.z };
    float col[3] = { pl.color.x, pl.color.y, pl.color.z };
    float intensity = pl.intensity;
    float range = pl.range;

    ImGui::Text("Point light %d", i);
    ImGui::DragFloat3("Pos", pos, 0.1f);
    ImGui::ColorEdit3("Col", col);
    ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.0f, 10.0f);
    ImGui::DragFloat("Range", &range, 0.5f, 0.1f, 100.0f);
    ImGui::PopID();
  }

  ImGui::End();
}

} // namespace forge
