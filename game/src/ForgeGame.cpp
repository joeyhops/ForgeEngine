#include "ForgeGame.h"

#include <forge/AssetManager.h>
#include <forge/Logger.h>
#include <forge/PhysicsWorld.h>
#include <forge/AnimationClip.h>
#include <forge/Events.h>
#include <forge/EventBus.h>
#include <forge/FlagManager.h>
#include <forge/DebugUI.h>
#include <forge/DebugDraw.h>
#include <forge/LuaState.h>
#include <forge/CombatSystem.h>
#include <forge/AnimGraph.h>
#include <forge/Animator.h>
#include <forge/map/GeometryGenerator.h>
#include <forge/map/MapGeometryTypes.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <sol/forward.hpp>

#include <memory>
#include <algorithm>
#include <sstream>
#include <fstream>
#include "GameFlags.h"

ForgeGame::ForgeGame()
  : forge::Application(1280, 720, "Forge Engine - Soulslike demo")
{}

void ForgeGame::onInit() {
  LOG_INFO("[Game] Initializing ForgeGame");
#ifdef __WIN32__
  forge::AssetManager::setAssetRoot("../../../../assets/");
#else
  forge::AssetManager::setAssetRoot("assets/");
#endif

  setupRenderer();
  getFlags().loadFromFile(k_saveFilePath);
  forge::AssetManager::loadWeaponDefs("data/weapons.json");
  forge::AssetManager::loadMovesetDefs("data/movesets.json");

  setupPlayer();
  setupEnemy();

  registerEntityFactories();

  setupLevel(m_initialMap);
  setupScripts();

#ifdef __APPLE__
  m_debugLineShader = forge::AssetManager::loadShader(
  "shaders/mac/debug_line.vert", 
  "shaders/mac/debug_line.frag"
  );
  m_brushShader = forge::AssetManager::loadShader(
    "shaders/mac/brush.vert",
    "shaders/mac/brush.frag"
  );
#else
  m_debugLineShader = forge::AssetManager::loadShader(
  "shaders/win/debug_line.vert", 
  "shaders/win/debug_line.frag"
  );
  m_brushShader = forge::AssetManager::loadShader(
    "shaders/win/brush.vert",
    "shaders/win/brush.frag"
  );
#endif

  forge::DebugDraw::init(m_debugLineShader.get());

  forge::EventBus::subscribe<forge::EntityHitEvent>([this](const forge::EntityHitEvent& e) {
    bool playerWasHit = (e.defenderName == "player");
    bool heavyHit = (e.damage >= 150.0f);

    float stopDuration = heavyHit ? k_hitStopHeavy : k_hitStopLight;
    m_hitStopTimer = std::max(m_hitStopTimer, stopDuration);

    if (playerWasHit)
      m_hitFlashAlpha = 1.0f;

    if (heavyHit) {
      m_shakeTimer = k_shakeDuration;
      m_shakeMagnitude = k_shakeMag;
    }

    glm::vec3 spawnPos = e.hitPosition + glm::vec3(0.0f, 1.2f, 0.0f);
    m_damageNumbers.push_back({
      spawnPos,
      e.damage,
      k_damNumberLifetime,
      heavyHit 
    });
  });
  forge::EventBus::subscribe<forge::RestEvent>([this](const forge::RestEvent& e) {
    for (const auto& bf : m_bonfires) {
      if (bf.bonfireId == e.bonfireId) {
        m_lastBonfirePos = bf.position;
        break;
      }
    }
    LOG_INFO("[Game] Bonfire {} rested - respawn pos updated", e.bonfireId);
  });

  LOG_INFO("[Game] init complete");
}

void ForgeGame::initHUD() {
#ifdef __APPLE__
  m_hudShader = forge::AssetManager::loadShader("shaders/mac/hud.vert", "shaders/mac/hud.frag");
#else
  m_hudShader = forge::AssetManager::loadShader("shaders/win/hud.vert", "shaders/win/hud.frag");
#endif

  // Unit quad: btm-left origin, covers [0,1]x[0,1]
  // The vertex shader maps it to pixel space via u_rect
  const float verts[] = {
    0.0f, 0.0f,
    1.0f, 0.0f,
    1.0f, 1.0f,
    0.0f, 0.0f,
    1.0f, 1.0f,
    0.0f, 1.0f,
  };

  glGenVertexArrays(1, &m_hudVAO);
  glGenBuffers(1, &m_hudVBO);

  glBindVertexArray(m_hudVAO);
  glBindBuffer(GL_ARRAY_BUFFER, m_hudVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

  glBindVertexArray(0);

  const std::string fontPath = forge::AssetManager::getAssetRoot() + "fonts/Lato-Regular.ttf";
  std::ifstream fontFile(fontPath, std::ios::binary | std::ios::ate);
  if (fontFile.is_open()) {
    size_t size = fontFile.tellg();
    fontFile.seekg(0);
    std::vector<uint8_t> fontData(size);
    fontFile.read(reinterpret_cast<char*>(fontData.data()), size);

    // Bake at 32PX - drawHUDText scales quads to the requested size at draw time
    std::vector<uint8_t> bitmap(k_fontAtlasW * k_fontAtlasH);
    stbtt_BakeFontBitmap(fontData.data(), 0, 32.0f,
                         bitmap.data(), k_fontAtlasW, k_fontAtlasH, k_fontFirstChar, k_fontNumChars, m_glyphData);

    glGenTextures(1, &m_fontAtlasTexture);
    glBindTexture(GL_TEXTURE_2D, m_fontAtlasTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED,
             k_fontAtlasW, k_fontAtlasH,
             0, GL_RED, GL_UNSIGNED_BYTE, bitmap.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
  } else {
    LOG_WARN("[HUD] Font not found at '{}' - text rendering disabled", fontPath);
  }

  // Dynamic text vbo (6 verts x 4 floats x up to 512 chars per draw call)
  glGenVertexArrays(1, &m_textVAO);
  glGenBuffers(1, &m_textVBO);
  glBindVertexArray(m_textVAO);
  glBindBuffer(GL_ARRAY_BUFFER, m_textVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 4 * 6 * 512, nullptr, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
  glBindVertexArray(0);

#ifdef __APPLE__
  m_hudTextShader = forge::AssetManager::loadShader("shaders/mac/hud_text.vert", "shaders/mac/hud_text.frag");
#else
  m_hudTextShader = forge::AssetManager::loadShader("shaders/win/hud_text.vert", "shaders/win/hud_text.frag");
#endif
}

void ForgeGame::registerEntityFactories() {
  auto staticSolidFactory = [this](const forge::LevelEntity& ent,
                                   const forge::EntityGeometry& geom,
                                   forge::PhysicsWorld& physics,
                                   forge::LuaState& lua) {
    forge::EntityInstance inst;
    inst.renderObjects = buildRenderObjects(geom);
    if (!geom.collisionPositions.empty()) {
      forge::Transform ident; // Static identity transform
      inst.rigidBody = std::make_unique<forge::RigidBodyComponent>(
        physics, ident, geom.collisionPositions, geom.collisionIndices, 0.0f
      );
    }
    return inst;
  };
  m_assembler.registerFactory("worldspawn", staticSolidFactory);
  m_assembler.registerFactory("func_wall", staticSolidFactory);
  m_assembler.registerFactory("func_group", staticSolidFactory);

  m_assembler.registerFactory("func_illusionary", [this](const forge::LevelEntity& ent,
                                                         const forge::EntityGeometry& geom,
                                                         forge::PhysicsWorld& physics,
                                                         forge::LuaState& lua) {
    forge::EntityInstance inst;
    inst.renderObjects = buildRenderObjects(geom);                          
    LOG_INFO("[Factory] Loaded func_illusionary");
    return inst;                          
  });

  m_assembler.registerFactory("trigger_once", [this](const forge::LevelEntity& ent,
                                                     const forge::EntityGeometry& geom,
                                                     forge::PhysicsWorld& physics,
                                                     forge::LuaState& lua) {
    forge::EntityInstance inst;
    if (!geom.collisionPositions.empty()) {
      std::string target = ent.getProperty("target");
      inst.trigger = std::make_unique<forge::TriggerVolume>(
        physics, geom.collisionPositions,
        [this, target]() { // onEnter
          LOG_INFO("[Trigger] trigger_once entered! Firing target: {}", target);
          forge::EventBus::publish(forge::ScriptEvent{ "triggerActivated", target });
        }
      );
    }
    return inst;
  });

  m_assembler.registerFactory("trigger_multiple", [this](const forge::LevelEntity& ent,
                                                         const forge::EntityGeometry& geom,
                                                         forge::PhysicsWorld& physics,
                                                         forge::LuaState& lua) {
    forge::EntityInstance inst;
    if (!geom.collisionPositions.empty()) {
      std::string target = ent.getProperty("target");
      inst.trigger = std::make_unique<forge::TriggerVolume>(
        physics, geom.collisionPositions,
        [target]() {
          LOG_INFO("[Trigger] trigger_multiple entered! Firing target: {}", target);
          forge::EventBus::publish(forge::ScriptEvent{ "triggerActivated", target });
        }
      ); 
    }
    return inst;
  });

  m_assembler.registerFactory("flag_trigger", [this](const forge::LevelEntity& ent,
                                                     const forge::EntityGeometry& geom,
                                                     forge::PhysicsWorld& physics,
                                                     forge::LuaState& lua) {
    forge::EntityInstance inst;
    int flagId = ent.getInt("flag_id", -1);
    bool once = ent.getBool("trigger_once", true);
    float radius = ent.getFloat("radius", 2.0f);
    if (flagId >= 0) {
      inst.trigger = std::make_unique<forge::TriggerVolume>(
        physics, ent.origin, radius,
        [this, flagId, once]() {
          getFlags().set(flagId, true);
          // Optionally: if once, disable trigger
        }
      );
    }
    return inst;
  });

                             
  // Basic point entities
  auto pointFactory = [](const forge::LevelEntity& ent,
                         const forge::EntityGeometry& geom,
                         forge::PhysicsWorld& physics,
                         forge::LuaState& lua) {
    return forge::EntityInstance();
  };
  m_assembler.registerFactory("info_player_start", pointFactory);
  m_assembler.registerFactory("enemy_spawn", pointFactory);
  m_assembler.registerFactory("bonfire", pointFactory);
  m_assembler.registerFactory("weapon_pickup", pointFactory);
  m_assembler.registerFactory("fog_gate", pointFactory);
  m_assembler.registerFactory("patrol_waypoint", pointFactory);
}

void ForgeGame::drawHUDText(float x, float y, float pixelHeight,
                            const char* text, float r, float g, float b, float a)
{
  if (!m_fontAtlasTexture || !m_hudTextShader) return;

  const float scale = pixelHeight /32.0f;

  std::vector<float> verts;
  verts.reserve(strlen(text) * 6 * 4);

  float cx = x, cy = y;
  while (*text) {
    int c = (unsigned char)*text++;
    if (c < k_fontFirstChar || c >= k_fontFirstChar + k_fontNumChars) continue;

    stbtt_aligned_quad q;
    stbtt_GetBakedQuad(m_glyphData, k_fontAtlasW, k_fontAtlasH,
                       c - k_fontFirstChar, &cx, &cy, &q, 1);

    auto sx = [&](float v) { return x + (v - x) * scale; };
    auto sy = [&](float v) { return y + (v - y) * scale; };

    // Two triangles per glyph (pos in screen space and UVs from atlas)
    float quad[6][4] = {
      { sx(q.x0), sy(q.y0), q.s0, q.t0 },
      { sx(q.x1), sy(q.y0), q.s1, q.t0 },
      { sx(q.x1), sy(q.y1), q.s1, q.t1 },
      { sx(q.x0), sy(q.y0), q.s0, q.t0 },
      { sx(q.x1), sy(q.y1), q.s1, q.t1 },
      { sx(q.x0), sy(q.y1), q.s0, q.t1 },
    };
    for (auto& v : quad)
      verts.insert(verts.end(), std::begin(v), std::end(v));
  }

  if (verts.empty()) return;

  glm::mat4 proj = glm::ortho(0.0f, (float)getWidth(),
                              (float)getHeight(), 0.0f);

  m_hudTextShader->bind();
  m_hudTextShader->setMat4("u_projection", proj);
  m_hudTextShader->setVec4("u_color", glm::vec4(r, g, b, a));
  m_hudTextShader->setInt("u_fontAtlas", 0);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_fontAtlasTexture);

  glBindVertexArray(m_textVAO);
  glBindBuffer(GL_ARRAY_BUFFER, m_textVBO);
  glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(float), verts.data());
  glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(verts.size() / 4));
  glBindVertexArray(0);

  m_hudTextShader->unbind();
}

void ForgeGame::drawHUDRect(float x, float y, float w, float h,
                            float r, float g, float b, float a) {
  glm::mat4 proj = glm::ortho(
    0.0f, (float)getWidth(),
    (float)getHeight(), 0.0f
  );
  m_hudShader->setMat4("u_projection", proj);
  m_hudShader->setVec4("u_rect", glm::vec4(x, y, w, h));
  m_hudShader->setVec4("u_color", glm::vec4(r, g, b, a));

  glBindVertexArray(m_hudVAO);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glBindVertexArray(0);
}

void ForgeGame::drawHUDBar(float x, float y, float w, float h,
                           float fill, float r, float g, float b) {
  drawHUDRect(x - 1.0f, y - 1.0f, w + 2.0f, h + 2.0f, 0.0f, 0.0f, 0.0f, 0.75f);
  if (fill > 0.0f)
    drawHUDRect(x, y, w * glm::clamp(fill, 0.0f, 1.0f), h, r, g, b, 0.85f);
}

void ForgeGame::onShutdown() {
  getFlags().saveToFile(k_saveFilePath);
  forge::AssetManager::printStats();
  forge::AssetManager::clear();
}

void ForgeGame::setupRenderer() {
  // Shader and camera
#ifdef __APPLE__
  m_shader = forge::AssetManager::loadShader(
    "shaders/mac/basic.vert",
    "shaders/mac/basic.frag"
  );
  m_skinnedShader = forge::AssetManager::loadShader("shaders/mac/skinned.vert",
                                                    "shaders/mac/basic.frag"); 
#else
  m_shader = forge::AssetManager::loadShader(
    "shaders/win/basic.vert",
    "shaders/win/basic.frag"
  );
  m_skinnedShader = forge::AssetManager::loadShader("shaders/win/skinned.vert",
                                                    "shaders/win/basic.frag"); 
#endif
  // Third person camera - positioned behind and above player
  // angled down to see the level.
  float aspect = (float)getWidth() / (float)getHeight();
  m_camera = std::make_unique<forge::Camera>(60.0f, aspect, 0.1f, 300.0f);
  m_tpCamera = std::make_unique<forge::ThirdPersonCamera>(*m_camera, getPhysics());

  initHUD();
  // Hide and capture cursor so all mouse mvmt drives the camera
  // ImGui panels remain accessible via F-key toggles and keyboard nav
  glfwSetInputMode(getWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

std::vector<forge::MapRenderObject> ForgeGame::buildRenderObjects(const forge::EntityGeometry& geom) {
  std::vector<forge::MapRenderObject> objects;
  for (const auto& surf : geom.surfaces) {
    std::vector<forge::Vertex> meshVerts;
    meshVerts.reserve(surf.vertices.size());
    for (const auto& mv : surf.vertices) {
      forge::Vertex v;
      v.position[0] = mv.position.x; v.position[1] = mv.position.y; v.position[2] = mv.position.z;
      v.normal[0] = mv.normal.x; v.normal[1] = mv.normal.y; v.normal[2] = mv.normal.z;
      v.texCoord[0] = mv.texCoord.x; v.texCoord[1] = mv.texCoord.y;
      v.tangent[0] = mv.tangent.x; v.tangent[1] = mv.tangent.y; v.tangent[2] = mv.tangent.z;
        v.tangent[3] = mv.tangent.w;
      meshVerts.push_back(v);
    }
    forge::MapRenderObject ro;
    ro.mesh = std::make_shared<forge::Mesh>(meshVerts, surf.indices);
    ro.albedo = forge::AssetManager::loadMapTexture(surf.textureName);
    ro.normalMap = forge::AssetManager::loadMapNormalTexture(surf.textureName);
    ro.roughnessMap = forge::AssetManager::loadMapRoughnessTexture(surf.textureName);
    ro.metallicMap = forge::AssetManager::loadMapMetallicTexture(surf.textureName);
    ro.aoMap = forge::AssetManager::loadMapAOTexture(surf.textureName);
    objects.push_back(ro);
  }
  return objects;
}

static std::shared_ptr<forge::StateMachineNode> buildCharacterGraph(
  std::shared_ptr<forge::AnimationClip> idleClip,
  std::shared_ptr<forge::AnimationClip> walkClip, 
  std::shared_ptr<forge::AnimationClip> sprintClip, // Optional - no sprint if null
  std::shared_ptr<forge::AnimationClip> attackClip,
  std::shared_ptr<forge::AnimationClip> dodgeClip, // optional - no dodge if null
  std::shared_ptr<forge::AnimationClip> deathClip,
  const std::string& ownerName)
{
  using namespace forge;

  if (!idleClip || !walkClip || !attackClip || !deathClip) {
    LOG_ERROR("[Game] buildCharacterGraph('{}') - required clip null (need idle/walk/attack/death)", ownerName);
    return nullptr;
  }
  auto idleNode = std::make_shared<ClipNode>(idleClip, true);
  auto walkNode = std::make_shared<ClipNode>(walkClip, true); //todo walk clip
  idleNode->setOwnerName(ownerName);
  walkNode->setOwnerName(ownerName);

  auto locomotionNode = std::make_shared<Blend1DNode>("moveSpeed");
  locomotionNode->addEntry(0.0f, idleNode);
  locomotionNode->addEntry(1.0f, walkNode);

  if (sprintClip) {
    auto sprintNode = std::make_shared<ClipNode>(sprintClip, true);
    sprintNode->setOwnerName(ownerName);
    locomotionNode->addEntry(2.0f, sprintNode);
  }

  // atk
  auto attackNode = std::make_shared<ClipNode>(attackClip, false);
  attackNode->setOwnerName(ownerName);
  attackNode->setActionKey("r1");

  // death
  auto deathNode = std::make_shared<ClipNode>(deathClip, false);
  deathNode->setOwnerName(ownerName);

  // graph root
  auto root = std::make_shared<StateMachineNode>();
  root->addState("Locomotion", locomotionNode);
  root->addState("Attacking", attackNode);
  root->addState("Dead", deathNode);

  // Locomotion-> attacking
  root->addTransition({
    "Locomotion", "Attacking",
    [](AnimParamTable& p) {
      return p.consumeTrigger("attackR1") || p.consumeTrigger("attackR2");
    },
    0.1f
  });

  // attacking -> locomotion
  root->addTransition({
    "Attacking", "Locomotion",
    [attackNode](AnimParamTable&) {
      return attackNode->isFinished();
    },
    0.2f
  });

  if (dodgeClip) {
    auto dodgeNode = std::make_shared<ClipNode>(dodgeClip, false);
    dodgeNode->setOwnerName(ownerName);
    root->addState("Dodging", dodgeNode);

    root->addTransition({
      "Locomotion", "Dodging",
      [](AnimParamTable& p) { return p.consumeTrigger("dodge"); },
      0.0f
    });

    root->addTransition({
      "Dodging", "Locomotion",
      [dodgeNode](AnimParamTable&) { return dodgeNode->isFinished(); },
      0.5f 
    });
  }

  // Any -> dead
  root->addTransition({
    "", "Dead",
    [](AnimParamTable& p) { return p.getBool("isDead"); },
    0.3f
  });

  root->addTransition({
    "Dead", "Locomotion",
    [](AnimParamTable& p) { return !p.getBool("isDead"); },
    0.5f
  });

  root->setInitialState("Locomotion");
  return root;
}

void ForgeGame::setupPlayer() {
  m_player.skinnedModel = forge::AssetManager::loadSkinnedModel("models/characters/anim/base_skeleton.glb"); 
  m_player.transform = std::make_unique<forge::Transform>();
  m_player.transform->setPosition({ 0.0f, 0.0f, 0.0f });
  m_player.transform->setScale({ 0.01f, 0.01f, 0.01f });
  m_player.transform->setEulerAngles({ 0.0f, 180.0f, 0.0f });

  m_player.controller = std::make_unique<forge::CharacterController>(getPhysics(), *m_player.transform, 0.3f, 0.9f);

  // Animator
  m_player.animator = std::make_unique<forge::Animator>();
  m_player.animator->setOwnerName("player");
  m_player.animator->setSkeleton(m_player.skinnedModel.skeleton);

  // Load anim clips
  // Idle is embadded in the base model - load the first animation from it
  m_player.clips["idle"] = forge::AssetManager::loadAnimationClip("anim/movesets/sword/idle.glb", "idle");
  m_player.clips["walk"] = forge::AssetManager::loadAnimationClip("anim/movesets/sword/walk_f_2.glb", "walk");
  m_player.clips["sprint"] = forge::AssetManager::loadAnimationClip("anim/movesets/sword/sprint_f_rm.glb", "sprint");
  m_player.clips["attack_r1"] = forge::AssetManager::loadAnimationClip("models/characters/anim/ybot_slash.glb", "slash");
  m_player.clips["death"] = forge::AssetManager::loadAnimationClip("models/characters/anim/ybot_death.glb", "death");
  m_player.clips["dodge"] = forge::AssetManager::loadAnimationClip("anim/movesets/sword/roll_f_rm.glb", "dodge");
  
  if (m_player.clips["attack_r1"]) {
    auto& clip = m_player.clips["attack_r1"];
    clip->looping = false;
    clip->events.push_back({ 14.0f/60.0f, 22.0f/60.0f,
                            forge::AnimEventType::SpawnHitbox, "weapon_r" });
    clip->events.push_back({ 10.0f/60.0f, 10.0f/60.0f,
                            forge::AnimEventType::SoundOneShot, "sfx/swing_heavy.wav" });
    std::sort(clip->events.begin(), clip->events.end(),
              [](const forge::AnimEvent& a, const forge::AnimEvent& b) { return a.startTime < b.startTime; });
  }
 
  auto& dodgeClip = m_player.clips["dodge"];
  if (!dodgeClip) {
    // irrelevant dodge clip is present. regardless
    dodgeClip = m_player.clips["idle"];
    LOG_WARN("[Game] ybot_dodge.glb not found - using idle as dodge stub.");
  }
  if (dodgeClip) {
    dodgeClip->looping = false;
    if (dodgeClip->duration < 0.6f || m_player.clips.count("dodge")) {
      dodgeClip->events.push_back({ 0.08f, 0.38f,
                                  forge::AnimEventType::IFrame, "dodge_iframe" });
      std::sort(dodgeClip->events.begin(), dodgeClip->events.end(),
                [](const forge::AnimEvent& a, const forge::AnimEvent& b){ return a.startTime < b.startTime; });
    }
  }

  if (m_player.clips["death"])
    m_player.clips["death"]->looping = false;

  auto graph = buildCharacterGraph(
    m_player.clips["idle"], 
    m_player.clips["walk"],
    m_player.clips["sprint"],
    m_player.clips["attack_r1"], 
    m_player.clips["dodge"],
    m_player.clips["death"], 
    "player"
  );
  m_player.animator->setGraph(graph);

  m_player.combat = std::make_unique<forge::CombatComponent>(
    "player",
    500.0f,
    100.0f,
    80.0f
  );

  m_player.combat->setAnimator(m_player.animator.get());
  m_player.combat->setParamTable(&m_player.animator->getParams());

  // Wire up lua for player
  m_player.combat->onDeath = [this](){
    getLua().callFunction("onPlayerDeath");
  };
  m_player.combat->onHit = [this](const forge::HitEvent& h) {
    getLua().callFunction("onPlayerHit", h.damage, h.damageType);
  };

  m_player.equipment = std::make_unique<forge::EquipmentComponent>("player");
  m_player.equipment->setAnimator(m_player.animator.get());

  // CombatComponent onEquip
  m_player.equipment->onEquip = [this](forge::EquipmentComponent::Slot slot, const forge::WeaponDef* def) {
    if (slot == forge::EquipmentComponent::RIGHT_HAND) {
      m_player.combat->setWeapon(def);
      if (!def->meshPath.empty())
        m_player.weaponModel = forge::AssetManager::loadModel(def->meshPath);
      getLua().callFunction("onPlayerEquip", static_cast<int>(slot), def->id);
    }
  };

  m_player.equipment->onUnequip = [this](forge::EquipmentComponent::Slot slot) {
    if (slot == forge::EquipmentComponent::RIGHT_HAND) {
      m_player.combat->setWeapon(nullptr);
      m_player.weaponModel = forge::ModelData{};
      getLua().callFunction("onPlayerUnequip", static_cast<int>(slot));
    }
  };
  
  setupPlayerAnimEvents();

  getCombat().registerCombatant(m_player.combat.get());
  m_player.combat->setGhostObject(m_player.controller->getGhostObject());
  getLua().get()["playerCombat"] = m_player.combat.get();
  getLua().get()["playerTransform"] = m_player.transform.get();
  getLua().get()["playerAnim"] = &m_player.animator->getParams();
  getLua().get()["playerEquipment"] = m_player.equipment.get();

  getDebugUI().registerAnimator(m_player.animator.get(), "Player");
  getDebugUI().registerEquipmentComponent(m_player.equipment.get());

  LOG_INFO("[ForgeGame] Player ready - skeleton: {} bones",
           m_player.skinnedModel.skeleton.size());
}

void ForgeGame::setupPlayerAnimEvents() {
  forge::EventBus::subscribe<forge::AnimEventActivated>([this](const forge::AnimEventActivated& e) {
    if (e.ownerName != "player") return;
    if (e.type != forge::AnimEventType::RootMotionBegin) return;

    m_rmOverride = true;
    m_rmScale = 1.0f;
    m_rmAxes = { true, false, true };

    // Parse payload: "scale;axes;maxVel"
    if (!e.payload.empty()) {
      std::istringstream ss(e.payload);
      std::string token;
      int idx = 0;
      while (std::getline(ss, token, ';')) {
        if (idx == 0 && !token.empty()) m_rmScale = std::stof(token);
        if (idx == 1 && !token.empty()) {
          m_rmAxes.x = token.find('x') != std::string::npos;
          m_rmAxes.y = token.find('y') != std::string::npos;
          m_rmAxes.z = token.find('z') != std::string::npos;
        }
        idx++;
      }
    }
    LOG_INFO("[RM] RootMotionBegin scale={:.2f} axes={}{}{}", m_rmScale,
             m_rmAxes.x ? "x" : "",m_rmAxes.y ? "y" : "",m_rmAxes.z ? "z" : "");
  });

  forge::EventBus::subscribe<forge::AnimEventDeactivated>([this](const forge::AnimEventDeactivated& e) {
    if (e.ownerName != "player") return;
    if (e.type != forge::AnimEventType::RootMotionEnd) return;
    m_rmOverride = false;
    m_rmScale = 1.0f;
    m_rmAxes = { true, false, true };
    LOG_INFO("[RM] RootMotionEnd");
  });

  forge::EventBus::subscribe<forge::AnimEventActivated>([this](const forge::AnimEventActivated& e) {
    if (e.ownerName != "player") return;
    if (e.type != forge::AnimEventType::RootMotionScale) return;
    if (!e.payload.empty()) m_rmScale = std::stof(e.payload);
  });

  forge::EventBus::subscribe<forge::AnimEventActivated>([this](const forge::AnimEventActivated& e) {
    if (e.ownerName != "player") return;
    if (e.type == forge::AnimEventType::SetKinematic) {
      m_kinematicMode = true;
      m_player.controller->setGravity(0.0f);
    } else if (e.type == forge::AnimEventType::RestorePhysics) {
      m_kinematicMode = false;
      m_player.controller->setGravity(-20.0f);
    } else if (e.type == forge::AnimEventType::LockInput) {
      m_inputLocked = true;
    } else if (e.type == forge::AnimEventType::UnlockInput) {
      m_inputLocked = false;
    }
  });
}

void ForgeGame::setupEnemy() {
  // Reuse the same Y Bot mesh — different transform, same skeleton
  m_enemy.skinnedModel = forge::AssetManager::loadSkinnedModel(
    "models/characters/anim/ybot_idle.glb");
 
  constexpr float k_enemyCapsuleHalfHeight = 0.75f;
  m_enemy.transform = std::make_unique<forge::Transform>();
  m_enemy.transform->setPosition({ 0.0f, 0.0f, -4.0f });
  m_enemy.transform->setScale({ 0.01f, 0.01f, 0.01f });
  m_enemy.transform->setEulerAngles({ 90.0f, 0.0f, 0.0f });
 
  m_enemy.controller = std::make_unique<forge::CharacterController>(
    getPhysics(), *m_enemy.transform, 0.3f, 0.9f);

  // Animator
  m_enemy.animator = std::make_unique<forge::Animator>();
  m_enemy.animator->setOwnerName("enemy");
  m_enemy.animator->setSkeleton(m_enemy.skinnedModel.skeleton);
 
  // Share clips with the player — same file, same data
  m_enemy.clips["idle"] = forge::AssetManager::loadAnimationClip("models/characters/anim/ybot_idle.glb", "idle"); 
  m_enemy.clips["walk"] = forge::AssetManager::loadAnimationClip("models/characters/anim/ybot_walk.glb", "walk"); 
  m_enemy.clips["attack_r1"] = forge::AssetManager::loadAnimationClip("models/characters/anim/ybot_slash.glb", "slash"); 
  m_enemy.clips["death"] = forge::AssetManager::loadAnimationClip("models/characters/anim/ybot_death.glb", "death"); 

  auto graph = buildCharacterGraph(
    m_enemy.clips["idle"], 
    m_enemy.clips["walk"],
    nullptr,
    m_enemy.clips["attack_r1"], 
    nullptr,
    m_enemy.clips["death"], 
    "enemy"
  );
  m_enemy.animator->setGraph(graph);
  // Combat
  m_enemy.combat = std::make_unique<forge::CombatComponent>(
    "enemy", 400.0f, 100.0f, 60.0f);
 
  m_enemy.combat->setAnimator(m_enemy.animator.get());
  m_enemy.combat->setParamTable(&m_enemy.animator->getParams());

  m_enemy.combat->onDeath = [this]() {
    m_enemy.animator->getParams().setBool("isDead", true);
    getLua().callFunction("onEnemyDeath");
  };
  
  m_enemy.combat->onHit = [this](const forge::HitEvent& h) {
    getLua().callFunction("onEnemyHit", h.damage, h.damageType);
  };

  // AI
  m_enemy.ai = std::make_unique<forge::AIComponent>(
    "enemy", *m_enemy.transform, *m_enemy.combat);

  m_enemy.equipment = std::make_unique<forge::EquipmentComponent>("enemy");
  m_enemy.equipment->setAnimator(m_enemy.animator.get());
 
  m_enemy.equipment->onEquip = [this](forge::EquipmentComponent::Slot slot, const forge::WeaponDef* def) {
    if (slot == forge::EquipmentComponent::RIGHT_HAND) {
      m_enemy.combat->setWeapon(def);
      if (!def->meshPath.empty())
        m_enemy.weaponModel = forge::AssetManager::loadModel(def->meshPath);
      getLua().callFunction("onEnemyEquip", static_cast<int>(slot), def->id);
    }
  };

  m_enemy.equipment->onUnequip = [this](forge::EquipmentComponent::Slot slot) {
    if (slot == forge::EquipmentComponent::RIGHT_HAND) {
      m_enemy.combat->setWeapon(nullptr);
      m_enemy.weaponModel = forge::ModelData{};
      getLua().callFunction("onEnemyUnequip", static_cast<int>(slot));
    }
  };

  getCombat().registerCombatant(m_enemy.combat.get());
  m_enemy.combat->setGhostObject(m_enemy.controller->getGhostObject());
  getDebugUI().registerAnimator(m_enemy.animator.get(), "Enemy");
  getDebugUI().registerAIComponent(m_enemy.ai.get());

  getLua().get()["enemyCombat"] = m_enemy.combat.get();
  getLua().get()["enemyTransform"] = m_enemy.transform.get();
  getLua().get()["enemyAnim"] = &m_enemy.animator->getParams();
  getLua().get()["enemyAI"] = m_enemy.ai.get();
  getLua().get()["enemyEquipment"] = m_enemy.equipment.get();

  LOG_INFO("[ForgeGame] Enemy ready");
}

void ForgeGame::setupLevel(const std::string& levelName) {
  const std::string root = forge::AssetManager::getAssetRoot();
  const std::string mapPath = root + "levels/" + levelName + ".map";

  m_levelData = forge::LevelLoader::load(mapPath);
  m_enemy.active = false;

  if (!m_levelData.valid) {
    throw std::runtime_error("[Game] Failed to load level map: " + mapPath);
  }

  m_mapScene = std::make_unique<forge::MapScene>();
  m_mapEntities.clear(); // Clear entities from previous level

  forge::GeometryGenerator gen;
  forge::GeometrySettings settings;

  // The master assembly loop
  for (const auto& ent : m_levelData.entities) {
    if (!m_assembler.hasFactory(ent.classname)) {
      if (ent.classname != "worldspawn" && ent.classname.find("func_") != 0) {
        LOG_WARN("[Game] No factory registered for entity: {}", ent.classname);
      }
      continue;
    }

    // 1. Generate geometry
    forge::EntityGeometry geom;
    if (!ent.brushes.empty()) {
      geom = gen.processEntity(ent, settings);
    }

    // 2. Assemble the Entity (Render objects + Phys + Triggers)
    forge::EntityInstance inst = m_assembler.assemble(ent,
                                                      geom,
                                                      getPhysics(),
                                                      getLua());
    // 3. Push render objects to the MapScene so they're drawn
    for (auto& ro : inst.renderObjects)
      m_mapScene->renderObjects.push_back(ro);

    // 4. Game specific Routing (point entities)
    if (inst.classname == "info_player_start") {
      glm::vec3 origin = inst.origin;
      constexpr float k_playerCapsuleHalfHeight = 0.9f;
      origin.y += k_playerCapsuleHalfHeight;
      m_player.controller->warp(origin);
      if (ent.angle != 0.0f) {
        float rad = glm::radians(ent.angle);
        m_player.forward = glm::normalize(glm::vec3(sinf(rad), 0.0f, cosf(rad)));
        m_player.transform->setEulerAngles({ 0.0f, ent.angle, 0.0f });
      }
      LOG_INFO("[Game] Player spawned at {:.2f},{:.2f},{:.2f}", origin.x, origin.y, origin.z);
      if (m_player.equipment) {
        m_player.equipment->equip(forge::EquipmentComponent::RIGHT_HAND,
                                  "longsword");
      }
    } else if (inst.classname == "enemy_spawn") {
      m_enemy.respawns = ent.getBool("respawns", true);
      m_enemy.deathFlag = ent.getInt("death_flag", -1);
      bool shouldSpawn = !(m_enemy.deathFlag > 0 && getFlags().get(m_enemy.deathFlag));
      if (shouldSpawn) {
        m_enemy.active = true;
        glm::vec3 spawnPos = inst.origin;

        constexpr float k_capsuleHalfHeight = 0.75f;
        constexpr float k_rayStart = 2.0f;
        constexpr float k_rayLength = 10.0f;

        glm::vec3 rayFrom = spawnPos + glm::vec3(0.0f, k_rayStart, 0.0f);
        glm::vec3 rayTo = spawnPos + glm::vec3(0.0f, -k_rayLength, 0.0f);

        forge::RaycastHit hit = getPhysics().raycast(rayFrom, rayTo);
        if (hit.hit) {
          spawnPos.y = hit.point.y + k_capsuleHalfHeight;
        } else {
          spawnPos.y -= k_capsuleHalfHeight;
        }
        m_enemySpawnPos = spawnPos;
        m_enemy.controller->warp(m_enemySpawnPos);

        // Patrol waypoints
        std::string group = ent.getProperty("patrol_group", "");
        if (!group.empty()) {
          m_enemy.ai->clearWaypoints();
          for (const auto* wp : m_levelData.getByClass("patrol_waypoint")) {
            if (wp->props.count("patrol_group") && wp->props.at("patrol_group") == group)
              m_enemy.ai->addWaypoint(wp->origin);
          }
        }

        // Equipment/Weapons
        std::string wepId = ent.getProperty("weaponId", "");
        bool dropOwn = (ent.getProperty("dropWeapon", "1") == "1");
        std::string otherWepId = !dropOwn ? ent.getProperty("dropId", "") : "";

        if (!dropOwn && !otherWepId.empty()) {
          m_enemy.ai->setWeaponConfig(wepId, false, otherWepId);
        } else {
          m_enemy.ai->setWeaponConfig(wepId, dropOwn);
        }

        if (!wepId.empty())
          m_enemy.equipment->equip(forge::EquipmentComponent::RIGHT_HAND, wepId);

        auto existingOnDeath = std::move(m_enemy.combat->onDeath);
        m_enemy.combat->onDeath = [this, existingOnDeath]() {
          if (existingOnDeath) existingOnDeath();
          if (m_enemy.ai->shouldDropWeapon()) {
            glm::vec3 dropPos = m_enemy.transform->getPosition();
            if (m_enemy.ai->dropsOwnWeapon()) {
              spawnWeaponPickup(dropPos, m_enemy.ai->getWeaponId(), false);
            } else if (!m_enemy.ai->getDropId().empty()) {
              spawnWeaponPickup(dropPos, m_enemy.ai->getDropId(), false);
            }
          }
          if (!m_enemy.respawns && m_enemy.deathFlag > 0) {
            getFlags().set(m_enemy.deathFlag, true);
            getFlags().saveToFile(k_saveFilePath);
            LOG_INFO("[Game] Enemy permanently killed - flag {} set", m_enemy.deathFlag);
          }
        };
        LOG_INFO("[Game] Enemy spawned at {:.2f},{:.2f},{:.2f}", spawnPos.x, spawnPos.y, spawnPos.z);
      } else {
        LOG_INFO("[Game] Enemy has been killed permanently, skipping...");
      }
    } else if (inst.classname == "bonfire") {
      BonfireVolume bf;
      bf.bonfireId = ent.getInt("bonfire_id", 0);
      bf.targetFlag = ent.getInt("targetFlag", 0);
      float radius = ent.getFloat("radius", 1.5f);

      bf.trigger = std::make_unique<forge::TriggerVolume>(getPhysics(), inst.origin, radius);
      bf.position = inst.origin + glm::vec3(0.0f, 0.1f, 0.0f);

      if (m_bonfires.empty())
        m_lastBonfirePos = bf.position;

      m_bonfires.push_back(std::move(bf));
    } else if (inst.classname == "fog_gate") {
      glm::vec3 pos = inst.origin;
      int requiredFlag = ent.getInt("requiredFlag", 0);
      float width = ent.getFloat("width", 2.0f);
      float height = ent.getFloat("height", 3.0f);
      glm::vec3 halfExtents(width * 0.5f, height * 0.5f, 0.3f);

      auto vol = std::make_unique<forge::TriggerVolume>(
          getPhysics(), pos, halfExtents,
          [this, requiredFlag, pos]() {
              if (requiredFlag == 0 || getFlags().get(requiredFlag)) return;

              glm::vec3 playerPos = m_player.transform->getPosition();
              glm::vec3 pushBack = glm::normalize(playerPos - pos) * 2.0f;
              m_player.controller->warp(playerPos + pushBack);

              forge::EventBus::publish(forge::ScriptEvent{ "fogGateLocked", "" });
          }
      );
      m_triggerVolumes.push_back(std::move(vol));
    } else if (inst.classname == "weapon_pickup") {
      std::string wepId = ent.getProperty("weaponId", "");
      bool respawns = ent.getBool("respawns", false);
      if (!wepId.empty())
        spawnWeaponPickup(inst.origin, wepId, respawns);
    }

    m_mapEntities.push_back(std::move(inst));
  }

  LOG_INFO("[Game] Map assembly complete: {} entities active", m_mapEntities.size());
}

void ForgeGame::setupScripts() {
  // Load order matters, flags before quests, attacks before combat
  getLua().get()["Flags"] = &getFlags();

  const std::string root = forge::AssetManager::getAssetRoot();
  getLua().loadScript(root + "scripts/events/flags.lua");
  getLua().loadScript(root + "scripts/combat/attacks.lua");
  getLua().loadScript(root + "scripts/combat/combat_rules.lua");
  getLua().loadScript(root + "scripts/equipment.lua");
  getLua().loadScript(root + "scripts/combat/player_combat.lua");
  getLua().loadScript(root + "scripts/ai/enemy_ai.lua");
  getLua().loadScript(root + "scripts/events/demo_quest.lua");

  getLua().get()["loadLevel"] = [this](const std::string& name) {
    m_levelPhysicsBody.reset();
    m_levelTransform.reset();
    setupLevel(name);
  };
  getLua().get().set_function("setPlayerWalkSpeed",
                              [this](float s){ m_playerWalkSpeed = s; });
  getLua().get().set_function("setPlayerSprintSpeed",
                              [this](float s){ m_playerSprintSpeed = s; });
  getLua().get().set_function("getPlayerWalkSpeed",
                              [this]() -> float { return m_playerWalkSpeed; });
  getLua().get().set_function("getPlayerSprintSpeed",
                              [this]() -> float { return m_playerSprintSpeed; });

  getLua().callFunction("onAIInit");
}

void ForgeGame::enterDeathSequence() {
  m_gameState = GameState::DeathSequence;
  m_gameStateDuration = 2.0f;
  m_gameStateTimer = m_gameStateDuration;
  LOG_INFO("[Game] Death sequence started");
}

void ForgeGame::enterYouDied() {
  m_gameState = GameState::YouDied;
  m_gameStateDuration = 2.5f;
  m_gameStateTimer = m_gameStateDuration;
  LOG_INFO("[Game] YOU DIED");
}

void ForgeGame::respawnPlayer() {
  m_player.combat->revive();
  m_player.controller->warp(m_lastBonfirePos);

  if (m_enemy.active && m_enemy.respawns) {
    m_enemy.combat->revive();
    m_enemy.controller->warp(m_enemySpawnPos);
    m_enemy.ai->reset();
  }

  m_lockedOn = false;
  m_tpCamera->setLockOnTarget(nullptr);

  m_rmOverride = false;
  m_rmScale = 1.0f;
  m_rmAxes = { true, false, true };
  m_inputLocked = false;

  m_gameState = GameState::Respawning;
  m_gameStateDuration = 1.0f;
  m_gameStateTimer = m_gameStateDuration;
  LOG_INFO("[Game] Player respawnned at bonfire");
}

void ForgeGame::onUpdate(float dt) {
  if (m_shakeTimer > 0.0f) {
    m_shakeTimer -= dt;
    float t = m_shakeTimer / k_shakeDuration;
    float mag = m_shakeMagnitude * t * t;

    // Random offset on X and Y only
    float ox = ((rand() % 1000) / 1000.0f - 0.5f) * 2.0f * mag;
    float oy = ((rand() % 1000) / 1000.0f - 0.5f) * 2.0f * mag;

    m_tpCamera->setTraumaOffset({ ox, oy, 0.0f });
  } else {
    m_tpCamera->setTraumaOffset({ 0.0f, 0.0f, 0.0f });
  }

  // Game State machine
  if (m_gameState != GameState::Playing) {
    m_gameStateTimer -= dt;

    if (m_gameState == GameState::DeathSequence) {
      m_player.animator->update(dt); // Keep dying!
      if (m_gameStateTimer <= 0.0f) enterYouDied();
    } else if (m_gameState == GameState::YouDied) {
      if (m_gameStateTimer <= 0.0f) respawnPlayer();
    } else if (m_gameState == GameState::Respawning) {
      m_player.animator->update(dt);
      if (m_gameStateTimer <= 0.0f) m_gameState = GameState::Playing;
    }
    return;
  }

  if (m_hitStopTimer > 0.0f) {
    m_hitStopTimer -= dt;
    return;
  }

  if (!m_player.combat->isAlive()) {
    enterDeathSequence();
    return;
  }

  for (auto& dn : m_damageNumbers) {
    dn.worldPos.y += k_damNumberRiseSpeed * dt;
    dn.lifetime -= dt;
  }
  // Remove expired numbers
  m_damageNumbers.erase(
    std::remove_if(m_damageNumbers.begin(), m_damageNumbers.end(),
                   [](const DamageNumber& d) { return d.lifetime <= 0.0f; }),
    m_damageNumbers.end()
  );
  handleInput(dt);

  m_player.animator->update(dt);
  if (m_enemy.active)
    m_enemy.animator->update(dt);

  applyMovement(dt);

  // Physics
  getPhysics().step(dt);

  for (auto& vol : m_triggerVolumes) {
    vol->update();
  }

  for (auto& bf : m_bonfires) {
    bf.trigger->update();
  }

  // Update factory assembled triggers
  for (auto& inst : m_mapEntities) {
    if (inst.trigger)
      inst.trigger->update();
  }

  // Bonfire interaction
  if (isKeyPressed(GLFW_KEY_B)) {
    for (auto& bf : m_bonfires) {
      if (bf.trigger->isOverlapping()) {
        m_player.combat->healToFull();
        if (bf.targetFlag != 0)
          getFlags().set(bf.targetFlag, true);
        getFlags().saveToFile(k_saveFilePath);
        forge::EventBus::publish(forge::RestEvent{ bf.bonfireId });
        getLua().callFunction("onBonfireRest", bf.bonfireId);
        LOG_INFO("[Level] Bonfire {} rest — player healed, flags saved", bf.bonfireId);
        break;
      }
    }
  }

  for (auto& pk : m_weaponPickups) {
    if (!pk.collected) pk.trigger->update();
  }

  if (isKeyPressed(GLFW_KEY_E)) {
    for (auto& pk : m_weaponPickups) {
      if (!pk.collected && pk.trigger->isOverlapping()) {
        m_player.equipment->equip(forge::EquipmentComponent::RIGHT_HAND, pk.weaponId);
        if (!pk.respawns) {
          pk.collected = true;
          pk.trigger->setEnabled(false);
        }
        LOG_INFO("[Level] Player picked up '{}'", pk.weaponId);
        break;
      }
    }
  }

  m_player.controller->syncTransform();

  if (m_player.equipment) {
    m_player.equipment->update(
      m_player.transform->getModelMatrix(),
      m_player.animator->getGlobalTransforms(),
      m_player.skinnedModel);
    
    auto playerPhase = m_player.combat->getAttackPhase();
    auto enemyPhase  = m_enemy.active ? m_enemy.combat->getAttackPhase()
                                      : forge::CombatComponent::AttackPhase::None;

    // Clear trails on new attack
    if (m_prevPlayerPhase == forge::CombatComponent::AttackPhase::None &&
        playerPhase       == forge::CombatComponent::AttackPhase::Startup)
        m_playerTrail.clear();

    if (m_prevEnemyPhase == forge::CombatComponent::AttackPhase::None &&
        enemyPhase       == forge::CombatComponent::AttackPhase::Startup)
        m_enemyTrail.clear();

    // Push to trail during active window
    if (playerPhase == forge::CombatComponent::AttackPhase::Active)
        m_playerTrail.push(m_player.combat->getHitboxTransform(),
                           m_player.combat->getHitboxHalfExtents());
    if (enemyPhase == forge::CombatComponent::AttackPhase::Active)
        m_enemyTrail.push(m_enemy.combat->getHitboxTransform(),
                          m_enemy.combat->getHitboxHalfExtents());

    // Store for next frame
    m_prevPlayerPhase = playerPhase;
    m_prevEnemyPhase  = enemyPhase;
    
    if (m_player.combat->getAttackPhase() != forge::CombatComponent::AttackPhase::None) {
      const forge::WeaponDef* wpnDef = m_player.equipment->getEquipped(forge::EquipmentComponent::RIGHT_HAND);
      glm::mat4 wpnWorld = m_player.equipment->getWeaponTransform(forge::EquipmentComponent::RIGHT_HAND);
      glm::vec3 halfExt(0.3f, 0.8f, 0.15f);
      m_player.combat->setHitboxTransform(wpnWorld, halfExt);
    }
  }

  m_tpCamera->update(m_player.transform->getPosition());

  glm::vec3 playerPos = m_player.transform->getPosition();
  m_player.combat->setWorldData(playerPos, m_player.forward);

  if (m_enemy.active) {
    m_enemy.controller->syncTransform();

    if (m_enemy.equipment && m_enemy.combat->isAlive()) {
      m_enemy.equipment->update(
        m_enemy.transform->getModelMatrix(),
        m_enemy.animator->getGlobalTransforms(),
        m_enemy.skinnedModel);


      if (m_enemy.combat->getAttackPhase() != forge::CombatComponent::AttackPhase::None) {
        const forge::WeaponDef* wpnDef = m_enemy.equipment->getEquipped(forge::EquipmentComponent::RIGHT_HAND);
        glm::mat4 wpnWorld = m_enemy.equipment->getWeaponTransform(forge::EquipmentComponent::RIGHT_HAND);
        glm::vec3 halfExt(0.3f, 0.8f, 0.15f);
        m_enemy.combat->setHitboxTransform(wpnWorld, halfExt);
      }
    }

    if (m_lockedOn) {
      m_lockOnEnemyPos = m_enemy.transform->getPosition() + glm::vec3(0.0f, 1.0f, 0.0f);
      if (!m_enemy.combat->isAlive()) {
        m_lockedOn = false;
        m_tpCamera->setLockOnTarget(nullptr);
        LOG_INFO("[Game] Lock-on released - enemy dead");
      }
    }

    glm::vec3 preMovePos = m_enemy.transform->getPosition();

    m_enemy.ai->update(dt, playerPos);

    glm::vec3 postMovePos = m_enemy.transform->getPosition();
    glm::vec3 displacement = postMovePos - preMovePos;
    displacement.y = 0.0f;
    m_enemy.transform->setPosition(preMovePos);
    m_enemy.controller->setWalkDirection(displacement);

    float enemyMoveSpeed = (glm::length(displacement) > 0.0001f) ? 1.0f : 0.0f;
    m_enemy.animator->getParams().setFloat("moveSpeed", enemyMoveSpeed);

  }
  // Input -> Lua
  bool j = isKeyDown(GLFW_KEY_J);
  bool k = isKeyDown(GLFW_KEY_K);
  bool l = isKeyDown(GLFW_KEY_L);
  auto inputTable = getLua().get().create_table();
  inputTable["attackLight"] = j && !m_prevInput.j;
  inputTable["attackHeavy"] = k && !m_prevInput.k;
  inputTable["guard"] = l;
  m_prevInput = { j, k, l };

  getLua().callFunction("onCombatUpdate", dt, inputTable);
  getCombat().update(dt);
  getLua().callFunction("onQuestUpdate", dt);

  if (isKeyPressed(GLFW_KEY_B))
    getLua().callFunction("onBonfireReset");
  if (isKeyPressed(GLFW_KEY_F9))
    getLua().loadScript(forge::AssetManager::getAssetRoot() + "scripts/combat/player_combat.lua");
}

void ForgeGame::onRender() {
  renderScene();
  renderDebugOverlays();
  renderHUD();
  renderDeathOverlay();
}

void ForgeGame::renderScene() {
  m_shader->bind();
  // Draw pickups
  for (const auto& pk : m_weaponPickups) {
    if (!pk.collected && pk.model.hasRenderData())
      drawModelAtMatrix(pk.model, pk.transform->getModelMatrix());
  }

  // Draw player weapon
  if (m_player.equipment && m_player.equipment->hasWeapon(forge::EquipmentComponent::RIGHT_HAND))
    drawModelAtMatrix(m_player.weaponModel, 
                      m_player.equipment->getWeaponTransform(forge::EquipmentComponent::RIGHT_HAND));

  // Draw enemy weapon
  if (m_enemy.active && m_enemy.equipment && m_enemy.combat->isAlive()
    && m_enemy.equipment->hasWeapon(forge::EquipmentComponent::RIGHT_HAND)) {
    drawModelAtMatrix(m_enemy.weaponModel,
                      m_enemy.equipment->getWeaponTransform(forge::EquipmentComponent::RIGHT_HAND));
  }
  m_shader->unbind();

  // Draw Native Map
  if (m_mapScene) {
    m_mapScene->render(*m_camera, m_brushShader.get());
  }

  m_skinnedShader->bind();
  drawSkinnedModel(m_player.skinnedModel, *m_player.transform, *m_player.animator);

  if (m_enemy.active) {
    forge::Transform enemyVisualTransform;
    enemyVisualTransform.setPosition(m_enemy.transform->getPosition());
    enemyVisualTransform.setScale(m_enemy.transform->getScale());
    enemyVisualTransform.setRotation(m_enemy.transform->getRotation());

    m_skinnedShader->setVec3("u_tint", glm::vec3(1.0f, 0.7f, 0.7f));
    drawSkinnedModel(m_enemy.skinnedModel, enemyVisualTransform, *m_enemy.animator);
    m_skinnedShader->setVec3("u_tint", glm::vec3(1.0f));
  } else {
    m_skinnedShader->setVec3("u_tint", glm::vec3(1.0f));
  }

  m_skinnedShader->unbind();
}

void ForgeGame::renderDebugOverlays() {
  static constexpr glm::vec3 kHitboxColors[] = {
    { 1.0f, 1.0f, 0.2f }, // Startup - yellow
    { 1.0f, 0.5f, 0.0f }, // Active - orange
    { 0.6f, 0.6f, 0.6f }, // Recovery - gray
    { 0.0f, 0.0f, 0.0f }, // None - n/a
  };
  static constexpr glm::vec3 kHitFlashColor = { 0.0f, 1.0f, 0.2f }; // green

  // Physics debug
  if (getDebugUI().isPhysicsDebugEnabled()) {
    // Player capsule - CharacterController gives the true phys center
    // via  getCapsuleCenter(), separate from the foot-position transform
    forge::DebugDraw::capsule(
      m_player.controller->getCapsuleCenter(), 
      m_player.controller->getRadius(), 
      m_player.controller->getCylinderHalfHeight(),
      { 0.0f, 1.0f, 0.0f } // green
    );

    glm::mat4 socketT = m_player.equipment->getWeaponTransform(forge::EquipmentComponent::RIGHT_HAND);
    glm::vec3 socketPos = glm::vec3(socketT[3]);
    forge::DebugDraw::sphere(socketPos, 0.04f, { 0.0f, 0.8f, 1.0f });
    forge::DebugDraw::axes(socketT, 0.12f);

    auto phase = m_player.combat->getAttackPhase();
    if (phase != forge::CombatComponent::AttackPhase::None) {
      glm::vec3 col = m_player.combat->getHitFlash()
        ? kHitFlashColor
        : kHitboxColors[static_cast<int>(phase)];
      forge::DebugDraw::boxOBB(
        m_player.combat->getHitboxTransform(),
        m_player.combat->getHitboxHalfExtents(),
        col
      );
      #ifdef SHOW_HITBOX_TRAILS
      for (int i = 0; i < m_playerTrail.count; i++) {
        int idx = (m_playerTrail.head - 1 - i + HitboxTrail::N) % HitboxTrail::N;
        float brightness = 1.0f - (float)i / (float)m_playerTrail.N;
        glm::vec3 trailCol = glm::vec3(1.0f, 0.5f, 0.0f) * brightness;
        forge::DebugDraw::boxOBB(
          m_playerTrail.transforms[idx],
          m_playerTrail.halfExtents[idx],
          trailCol
        );
      }
      #endif
    }


    if (m_enemy.active) {
      constexpr float kR = 0.3f;
      constexpr float kH = 0.45f;
      forge::DebugDraw::capsule(
        m_enemy.controller->getCapsuleCenter(), 
        kR, kH,
        { 1.0f, 0.3f, 0.3f }
      );


      auto phase = m_enemy.combat->getAttackPhase();
      if (phase != forge::CombatComponent::AttackPhase::None) {
        glm::vec3 col = m_enemy.combat->getHitFlash()
          ? kHitFlashColor
          : kHitboxColors[static_cast<int>(phase)];
        forge::DebugDraw::boxOBB(
          m_enemy.combat->getHitboxTransform(),
          m_enemy.combat->getHitboxHalfExtents(),
          col
        );
        #ifdef SHOW_HITBOX_TRAILS
        for (int i = 0; i < m_enemyTrail.count; i++) {
          int idx = (m_enemyTrail.head - 1 - i + HitboxTrail::N) % HitboxTrail::N;
          float brightness = 1.0f - (float)i / (float)m_enemyTrail.N;
          glm::vec3 trailCol = glm::vec3(1.0f, 0.5f, 0.0f) * brightness;
          forge::DebugDraw::boxOBB(
            m_enemyTrail.transforms[idx],
            m_enemyTrail.halfExtents[idx],
            trailCol
          );
        }
        #endif
      }
    }

    forge::DebugDraw::flush(m_camera->getViewProjection());
  }
}

void ForgeGame::renderHUD() {
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  m_hudShader->bind();

  const float barW = 200.0f;
  const float barH = 12.0f;
  const float staminaW = barW * 0.75f;
  const float staminaH = barH * 0.75f;
  const float marginX = 24.0f;
  const float marginY = (float)getHeight() - 60.0f;
  const float gap = 5.0f;

  float hpFill = m_player.combat->getHp() / m_player.combat->getMaxHp();
  float staFill = m_player.combat->getStamina() / m_player.combat->getMaxStamina();

  drawHUDBar(marginX, marginY, barW, barH, hpFill, 0.75f, 0.15f, 0.15f);
  drawHUDBar(marginX, marginY + barH + gap, staminaW, staminaH, staFill, 0.85f, 0.75f, 0.10f);

  if (m_enemy.active && m_enemy.combat->isAlive()) {
    const float bossW = 400.0f;
    const float bossH = 14.0f;
    float bossX = ((float)getWidth() - bossW) * 0.5f;
    float bossY = (float)getHeight() - 50.0f;
    float bossFill = m_enemy.combat->getHp() / m_enemy.combat->getMaxHp();
    drawHUDBar(bossX, bossY, bossW, bossH, bossFill, 0.80f, 0.20f, 0.20f);
  }

  m_hudShader->unbind();

  glEnable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);

  if (!m_damageNumbers.empty()) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float sw = (float)getWidth();
    float sh = (float)getHeight();
    glm::mat4 vp = m_camera->getViewProjection();

    for (const auto& dn : m_damageNumbers) {
      // Project to clip space
      glm::vec4 clip = vp * glm::vec4(dn.worldPos, 1.0f);
      if (clip.w <= 0.0f) continue; // behind camera

      // NDC
      glm::vec3 ndc = glm::vec3(clip) / clip.w;
      if (std::abs(ndc.x) > 1.0f || std::abs(ndc.y) > 1.0f) continue;

      // Screen pix (Y is flipped, NDC + 1 = top, screen 0 = top)
      float px = (ndc.x + 1.0f) * 0.5f * sw;
      float py = (1.0f - ndc.y) * 0.5f * sh;

      // Alpha fades over lifetime
      float alpha = std::clamp(dn.lifetime / k_damNumberLifetime, 0.0f, 1.0f);

      char buff[16];
      std::snprintf(buff, sizeof(buff), "%.0f", dn.damage);
      float fontSize = dn.heavy ? 22.0f : 16.0f;

      if (dn.heavy)
        drawHUDText(px, py, fontSize, buff, 1.0f, 0.82f, 0.16f, alpha);
      else
        drawHUDText(px, py, fontSize, buff, 1.0f, 1.0f, 1.0f, alpha);
    }

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
  }
}

void ForgeGame::renderDeathOverlay() {
  if (m_gameState != GameState::Playing) {
    float alpha = 0.0f;

    if (m_gameState == GameState::DeathSequence) {
      float elapsed = m_gameStateDuration - m_gameStateTimer;
      alpha = glm::clamp(elapsed / 1.5f, 0.0f, 1.0f);
    } else if (m_gameState == GameState::YouDied) {
      alpha = 1.0f;
    } else if (m_gameState == GameState::Respawning) {
      alpha = glm::clamp(m_gameStateTimer / m_gameStateDuration, 0.0f, 1.0f);
    }

    if (alpha > 0.0f) {
      glDisable(GL_DEPTH_TEST);
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

      m_hudShader->bind();
      drawHUDRect(0.0f, 0.0f, (float)getWidth(), (float)getHeight(),
                  0.0f, 0.0f, 0.0f, alpha * 0.90f);

      if (m_gameState == GameState::YouDied) {
        float textAlpha = glm::clamp(
          (m_gameStateDuration - m_gameStateTimer) / 1.0f, 0.0f, 1.0f);

        const char* title = "YOU DIED";
        float fontSize = 64.0f;

        float approxW = (float)strlen(title) * fontSize * 0.55f;
        float tx = ((float)getWidth() - approxW) * 0.5f;
        float ty = ((float)getHeight() - fontSize) * 0.5f;

        drawHUDText(tx, ty, fontSize, title, 0.71f, 0.08f, 0.08f, textAlpha);
      }

      m_hudShader->unbind();

      glEnable(GL_DEPTH_TEST);
      glDisable(GL_BLEND);
    }
  }
}

// Draw Skinned Model
void ForgeGame::drawSkinnedModel(const forge::SkinnedModelData& model,
                                 const forge::Transform& transform,
                                 const forge::Animator& animator)
{
  if (!model.mesh) return;

  glm::mat4 modelMat = transform.getModelMatrix();
  glm::mat4 mvp = m_camera->getViewProjection() * modelMat;

  m_skinnedShader->setMat4("u_mvp", mvp);
  m_skinnedShader->setMat4("u_model", modelMat);
  //m_skinnedShader->setVec3("u_tint", glm::vec3(1.0f));

  // Upload bone palette - the heart of skinning
  const auto& bones = animator.getBoneMatrices();
  if (!bones.empty()) {
    m_skinnedShader->setBool("u_hasBones", true);
    m_skinnedShader->setMat4Array(
      "u_boneMatrices",
      static_cast<int>(std::min(bones.size(), (size_t)forge::MAX_BONES)),
      bones.data()
    );
  } else {
    m_skinnedShader->setBool("u_hasBones", false);
  }

  if (model.hasTexture()) {
    model.texture->bind(0);
    m_skinnedShader->setInt("u_texture", 0);
    m_skinnedShader->setBool("u_hasTexture", true);
  } else {
    m_skinnedShader->setBool("u_hasTexture", false);
  }

  model.mesh->draw();
}

// Input - full rewrite
//
// Responsibilities:
//    - Mouse delta -> third person camera yaw/pitch
//    - Tab -> toggle lock-on to nearest enemy
//    - WASD -> Camera relative movement (no lock on)
//          -> strafe-relative movement (locked on)
//    - Shift -> sprint (moveSpeed param > 1.0 drives Blend1DNode)
//    - Space -> Dodge roll (AnimGraph trigger + timed velocity override)
//
// Writes CharacterController:setWalkDirection(velocity * dt) each frame.
// Never sets transform.position directly, controller owns X&Z and bullet owns Y.

void ForgeGame::handleInput(float dt) {
  if (isKeyPressed(GLFW_KEY_GRAVE_ACCENT)) {
    m_uiMouseMode = !m_uiMouseMode;
    glfwSetInputMode(getWindow(), GLFW_CURSOR,
                     m_uiMouseMode ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
    if (!m_uiMouseMode)
      m_firstMouse = true;
    LOG_INFO("[Game] UI Mouse Mode: {}", m_uiMouseMode ? "ON" : "OFF");
  }
  if (!m_player.combat->isAlive()) {
    m_moveDir = glm::vec3(0.0f);
    m_moveSpeed = 0.0f;
    return;
  }

  // Mouse delta
  double mx, my;
  glfwGetCursorPos(getWindow(), &mx, &my);

  if (!m_firstMouse && !m_uiMouseMode && !getDebugUI().isCapturingMouse()) {
    float dx = static_cast<float>(mx - m_prevMouseX);
    float dy = static_cast<float>(my - m_prevMouseY);
    m_tpCamera->applyMouseDelta(dx, dy);
  }
  m_firstMouse = false;
  m_prevMouseX = mx;
  m_prevMouseY = my;

  // Tab: Toggle lock on
  if (isKeyPressed(GLFW_KEY_TAB)) {
    if (m_lockedOn) {
      m_lockedOn = false;
      m_tpCamera->setLockOnTarget(nullptr);
      LOG_INFO("[Game] Lock-on released");
    } else {
      glm::vec3 pp = m_player.transform->getPosition();
      glm::vec3 ep = m_enemy.transform->getPosition();
      float dist = glm::length(ep - pp);
      if (dist < 15.0f && m_enemy.combat->isAlive()) {
        m_lockedOn = true;
        m_lockOnEnemyPos = ep + glm::vec3(0.0f, 1.0f, 0.0f);
        m_tpCamera->setLockOnTarget(&m_lockOnEnemyPos);
        LOG_INFO("[Game] Locked on to enemy at dist={:.1f}m", dist);
      }
    }
  }

  glm::vec3   move  = { 0, 0, 0 };

  bool isDodging = (m_player.animator->getCurrentStateName() == "Dodging");
  if (isDodging){
    m_player.animator->getParams().setFloat("moveSpeed", 0.0f);
    m_moveDir = glm::vec3(0.0f);
    m_moveSpeed = 0.0f;
    return;
  }

  if (!m_lockedOn) {
    glm::vec3 camFwd = m_tpCamera->getHorizontalForward();
    glm::vec3 camRight = m_tpCamera->getHorizontalRight();

    if (isKeyDown(GLFW_KEY_W)) move += camFwd;
    if (isKeyDown(GLFW_KEY_S)) move -= camFwd;
    if (isKeyDown(GLFW_KEY_A)) move -= camRight;
    if (isKeyDown(GLFW_KEY_D)) move += camRight;
  } else {
    // Lock-on: strafe around locked enemy
    glm::vec3 pp = m_player.transform->getPosition();
    glm::vec3 toTarget = m_lockOnEnemyPos - pp;
    toTarget.y = 0.0f;
    if (glm::length(toTarget) > 0.001f) toTarget = glm::normalize(toTarget);
    glm::vec3 strafeRight = glm::normalize(glm::cross(toTarget, glm::vec3(0.0f, 1.0f, 0.0f)));

    if (isKeyDown(GLFW_KEY_W)) move += toTarget;
    if (isKeyDown(GLFW_KEY_S)) move -= toTarget;
    if (isKeyDown(GLFW_KEY_A)) move -= strafeRight;
    if (isKeyDown(GLFW_KEY_D)) move += strafeRight;
  }

  // Sprint
  bool sprinting = isKeyDown(GLFW_KEY_LEFT_SHIFT);
  float walkSpeed = m_playerWalkSpeed;
  float sprintSpeed = m_playerSprintSpeed;
  float speed = sprinting ? sprintSpeed : walkSpeed;

  // Dodge

  if (isKeyPressed(GLFW_KEY_SPACE)&& !m_inputLocked && !m_player.combat->isAttacking()) {
    glm::vec3 dodgeDir = (glm::length(move) > 0.001f)
      ? glm::normalize(move)
      : m_player.forward;
    m_dodgeFacingAngle = atan2f(dodgeDir.x, dodgeDir.z);
    m_dodgePending = true;
    m_player.animator->getParams().setTrigger("dodge");
    LOG_INFO("[Game] Dodge initiated.");
  }

  if (glm::length(move) > 0.001f) {
    m_moveDir = glm::normalize(move);
    m_moveSpeed = sprinting ? m_playerSprintSpeed : m_playerWalkSpeed;

    glm::vec3 faceDir = m_lockedOn
      ? (m_lockOnEnemyPos - m_player.transform->getPosition())
      : glm::vec3(move);
    faceDir.y = 0.0f;
    if (glm::length(faceDir) > 0.001f) {
      float angle = atan2f(faceDir.x, faceDir.z);
      m_player.transform->setEulerAngles({ 0.0f, glm::degrees(angle), 0.0f });
      m_player.forward = glm::normalize(faceDir);
    }

    float moveSpeedParam = sprinting ? 2.0f : 1.0f;
    m_player.animator->getParams().setFloat("moveSpeed", moveSpeedParam);
  } else {
    m_moveDir = glm::vec3(0.0f);
    m_moveSpeed = 0.0f;
    m_player.animator->getParams().setFloat("moveSpeed", 0.0f);

    if (m_lockedOn) {
      glm::vec3 faceDir = m_lockOnEnemyPos - m_player.transform->getPosition();
      faceDir.y = 0.0f;
      if (glm::length(faceDir) > 0.001f) {
        float angle = atan2f(faceDir.x, faceDir.z);
        m_player.transform->setEulerAngles({ 0.0f, glm::degrees(angle), 0.0f });
        m_player.forward = glm::normalize(faceDir);
      }
    }
  }
}

void ForgeGame::applyMovement(float dt) {
  if (m_rmOverride && m_player.animator->getCurrentStateName() != "Dodging") {
    m_rmOverride = false;
    m_rmScale = 1.0f;
    m_rmAxes = { true, false, true };
  }
  if (!m_player.combat->isAlive()) {
    m_player.controller->setWalkDirection(glm::vec3(0.0f));
    return;
  }

  glm::vec3 displacement(0.0f);

  bool isDodging = (m_player.animator->getCurrentStateName() == "Dodging");
  bool useRM = m_player.animator->isRootMotionActive() || m_rmOverride || isDodging;

  if (useRM) {
    glm::vec3 localDelta = m_player.animator->getRootMotionDelta();
    localDelta *= m_player.transform->getScale().x;

    if (m_rmOverride) {
      if (!m_rmAxes.x) localDelta.x = 0.0f;
      if (!m_rmAxes.y) localDelta.y = 0.0f;
      if (!m_rmAxes.z) localDelta.z = 0.0f;
      localDelta *= m_rmScale;
    }

    float facingAngle = atan2f(m_player.forward.x, m_player.forward.z);

    if (isDodging)
      facingAngle = m_dodgeFacingAngle;

    glm::quat facing = glm::angleAxis(facingAngle, glm::vec3(0.0f, 1.0f, 0.0f));
    displacement = facing * localDelta;
  } else {
    displacement = m_moveDir * m_moveSpeed * dt;
  }

  m_player.controller->setWalkDirection(displacement);
  m_dodgePending = false;
}

void ForgeGame::spawnWeaponPickup(const glm::vec3& pos, const std::string& weaponId, bool respawns) {
  const forge::WeaponDef* def = forge::AssetManager::getWeaponDef(weaponId);
  if (!def) {
    LOG_ERROR("[Level] spawnWeaponPickup: unknown weaponId '{}'", weaponId);
    return;
  }

  WeaponPickup pickup;
  pickup.weaponId = weaponId;
  pickup.respawns = respawns;

  pickup.transform = std::make_unique<forge::Transform>();
  pickup.transform->setPosition(pos);

  // Load weapon mesh
  if (!def->meshPath.empty())
    pickup.model = forge::AssetManager::loadModel(def->meshPath);

  pickup.trigger = std::make_unique<forge::TriggerVolume>(getPhysics(), pos, 1.0f);

  m_weaponPickups.push_back(std::move(pickup));
  LOG_INFO("[Level] Spawned '{}' pickup at ({:.1f},{:.1f},{:.1f})",
           weaponId, pos.x, pos.y, pos.z);
}

void ForgeGame::drawModelAtMatrix(const forge::ModelData& model, const glm::mat4& matrix) {
  if (!model.hasRenderData()) return;

  glm::mat4 mvp = m_camera->getViewProjection() * matrix;
  m_shader->setMat4("u_mvp", mvp);
  m_shader->setMat4("u_model", matrix);
  m_shader->setVec3("u_tint", glm::vec3(1.0f));

  for (const auto& sub : model.subMeshes) {
    if (sub.texture) {
      sub.texture->bind(0);
      m_shader->setInt("u_texture", 0);
      m_shader->setBool("u_hasTexture", true);
    } else {
      m_shader->setBool("u_hasTexture", false);
    }
    sub.mesh->draw();
  }
}

