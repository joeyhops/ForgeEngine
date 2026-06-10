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
#include <forge/Animator.h>
#include <forge/map/GeometryGenerator.h>
#include <forge/map/MapGeometryTypes.h>
#include <forge/Renderer.h>
#include <forge/Material.h>
#include <forge/map/MapScene.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
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

  float aspect = (float)getWidth() / (float)getHeight();
  m_camera = std::make_unique<forge::Camera>(60.0f, aspect, 0.1f, 300.0f);
  m_tpCamera = std::make_unique<forge::ThirdPersonCamera>(*m_camera, getPhysics());

  initRenderer(forge::AssetManager::getAssetRoot());
  initHUD();
  glfwSetInputMode(getWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);

  getFlags().loadFromFile(k_saveFilePath);
  forge::AssetManager::loadWeaponDefs("data/weapons.json");
  forge::AssetManager::loadMovesetDefs("data/movesets.json");

  m_player.setup(getPhysics(), getLua(), getCombat(), getDebugUI());
  m_player.combat->setBodyCapsule(0.3f, 0.9f);
  m_enemy.setup(getPhysics(), getLua(), getCombat(), getDebugUI());
  m_enemy.combat->setBodyCapsule(0.3f, 0.75f);

  setupRootMotionAnimEvents();

  registerEntityFactories();

  setupLevel(m_initialMap);
  setupScripts();

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
  m_assembler.registerFactory("static_prop", pointFactory);
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
    ro.material = *forge::AssetManager::loadMaterialForTBTexture(surf.textureName);
    objects.push_back(ro);
  }
  return objects;
}

void ForgeGame::setupRootMotionAnimEvents() {
  forge::EventBus::subscribe<forge::AnimEventActivated>([this](const forge::AnimEventActivated& e) {
    if (e.ownerName != "player") return;
    if (e.type != forge::AnimEventType::RootMotionBegin) return;

    this->m_rmOverride = true;
    this->m_rmScale = 1.0f;
    this->m_rmAxes = { true, false, true };

    // Parse payload: "scale;axes;maxVel"
    if (!e.payload.empty()) {
      std::istringstream ss(e.payload);
      std::string token;
      int idx = 0;
      while (std::getline(ss, token, ';')) {
        if (idx == 0 && !token.empty()) this->m_rmScale = std::stof(token);
        if (idx == 1 && !token.empty()) {
          this->m_rmAxes.x = token.find('x') != std::string::npos;
          this->m_rmAxes.y = token.find('y') != std::string::npos;
          this->m_rmAxes.z = token.find('z') != std::string::npos;
        }
        idx++;
      }
    }
    LOG_INFO("[RM] RootMotionBegin scale={:.2f} axes={}{}{}", this->m_rmScale,
             this->m_rmAxes.x ? "x" : "",this->m_rmAxes.y ? "y" : "",this->m_rmAxes.z ? "z" : "");
  });

  forge::EventBus::subscribe<forge::AnimEventDeactivated>([this](const forge::AnimEventDeactivated& e) {
    if (e.ownerName != "player") return;
    if (e.type != forge::AnimEventType::RootMotionEnd) return;
    this->m_rmOverride = false;
    this->m_rmScale = 1.0f;
    this->m_rmAxes = { true, false, true };
    LOG_INFO("[RM] RootMotionEnd");
  });

  forge::EventBus::subscribe<forge::AnimEventActivated>([this](const forge::AnimEventActivated& e) {
    if (e.ownerName != "player") return;
    if (e.type != forge::AnimEventType::RootMotionScale) return;
    if (!e.payload.empty()) this->m_rmScale = std::stof(e.payload);
  });

  forge::EventBus::subscribe<forge::AnimEventActivated>([this](const forge::AnimEventActivated& e) {
    if (e.ownerName != "player") return;
    if (e.type == forge::AnimEventType::SetKinematic) {
      m_kinematicMode = true;
      m_player.controller->setGravity(0.0f);
    } else if (e.type == forge::AnimEventType::RestorePhysics) {
      m_kinematicMode = false;
      m_player.controller->setGravity(-20.0f);
    } 
  });
}

void ForgeGame::setupLevel(const std::string& levelName) {
  const std::string root = forge::AssetManager::getAssetRoot();
  const std::string mapPath = root + "levels/" + levelName + ".map";

  constexpr float k_playerCapsuleHalfHeight = 0.9f;

  m_levelData = forge::LevelLoader::load(mapPath);
  m_enemy.active = false;

  if (!m_levelData.valid) {
    throw std::runtime_error("[Game] Failed to load level map: " + mapPath);
  }

  m_mapScene = std::make_unique<forge::MapScene>();
  m_mapEntities.clear(); // Clear entities from previous level
  m_staticProps.clear();
  m_bonfires.clear();

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
      origin.y += k_playerCapsuleHalfHeight;
      m_player.controller->warpAndSettle(origin);
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
        m_enemy.spawnPos = spawnPos;
        m_enemy.controller->warp(m_enemy.spawnPos);

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
      bf.position = inst.origin + glm::vec3(0.0f, k_playerCapsuleHalfHeight, 0.0f);

      bf.lightColor = glm::vec3(
        ent.getFloat("light_r", 255) / 255.0f,
        ent.getFloat("light_g", 155) / 255.0f,
        ent.getFloat("light_b", 13) / 255.0f
      );
      bf.lightIntensity = ent.getFloat("light_intensity", 8.0f);
      bf.lightRange = ent.getFloat("light_range", 12.0f);

      std::string bfModel = ent.getProperty("model");
      if (!bfModel.empty()) {
        forge::ModelData model = forge::AssetManager::loadModel(bfModel);
        float bfScale = ent.getFloat("scale", 5.0f) * forge::LevelLoader::k_defaultMapScale;
        glm::mat4 T = glm::translate(glm::mat4(1.0f), ent.origin);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(bfScale));
        m_staticProps.push_back({ std::move(model), T * S });
      }

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
    } else if (inst.classname == "static_prop") {
      std::string modelPath = ent.getProperty("model");
      if (!modelPath.empty()) {
        forge::ModelData model = forge::AssetManager::loadModel(modelPath);
        float yaw = glm::radians(ent.getFloat("angle", 0.0f));
        float scale = ent.getFloat("scale", 1.0f) * forge::LevelLoader::k_defaultMapScale;

        glm::mat4 T = glm::translate(glm::mat4(1.0f), ent.origin);
        glm::mat4 R = glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f));

        glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(scale));
        m_staticProps.push_back({ std::move(model), T * R * S });
      }
    }

    m_mapEntities.push_back(std::move(inst));
  }

  getRenderer().getLights().clearPointLights();
  for (const auto& le : m_levelData.getByClass("light_point")) {
    forge::Light l;
    l.posOrDir = le->origin;
    l.color = glm::vec3(le->getFloat("r", 255) / 255.0f,
                        le->getFloat("g", 230) / 255.0f,
                        le->getFloat("b", 180) / 255.0f);
    l.intensity = le->getFloat("intensity", 1.0f);
    l.range = le->getFloat("radius", 5.0f);
    getRenderer().getLights().addPointLight(l);
  }

  for (const auto& bf : m_bonfires) {
    forge::Light bonfireLight;
    bonfireLight.posOrDir = bf.position + glm::vec3(0.0f, 1.2f, 0.0f); // offset above bonfire base
    bonfireLight.color = bf.lightColor;
    bonfireLight.intensity = bf.lightIntensity;
    bonfireLight.range = bf.lightRange;
    getRenderer().getLights().addPointLight(bonfireLight);
  }

  if (getRenderer().getLights().getPointLightCount() >= 6)
    LOG_WARN("Currently at %d or %d point lights",
             getRenderer().getLights().getPointLightCount(),
             getRenderer().getLights().k_maxPointLights);

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

  if (m_enemy.active && m_enemy.respawns) m_enemy.reset(); 

  m_lockedOn = false;
  m_tpCamera->setLockOnTarget(nullptr);

  m_rmOverride = false;
  m_rmScale = 1.0f;
  m_rmAxes = { true, false, true };
  m_player.inputLocked = false;

  m_gameState = GameState::Respawning;
  m_gameStateDuration = 1.0f;
  m_gameStateTimer = m_gameStateDuration;
  LOG_INFO("[Game] Player respawned at ({:.2f}, {:.2f}, {:.2f})",
          m_lastBonfirePos.x, m_lastBonfirePos.y, m_lastBonfirePos.z);
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

  m_input.update(getWindow(), m_tpCamera->getHorizontalForward(), m_player.forward);
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

  for (auto& pk : m_weaponPickups) {
    if (!pk.collected) pk.trigger->update();
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
        m_playerTrail.push(m_player.combat->getWorldCapsules());
    if (enemyPhase == forge::CombatComponent::AttackPhase::Active)
        m_enemyTrail.push(m_enemy.combat->getWorldCapsules());

    // Store for next frame
    m_prevPlayerPhase = playerPhase;
    m_prevEnemyPhase  = enemyPhase;
    
    if (m_player.combat->getAttackPhase() != forge::CombatComponent::AttackPhase::None) {
      const auto& localCaps = m_player.combat->getLocalCapsules();
      if (!localCaps.empty()) {
        glm::mat4 wpnWorld = m_player.equipment->getWeaponTransform(forge::EquipmentComponent::RIGHT_HAND);
        std::vector<forge::WorldCapsule> worldCaps;
        worldCaps.reserve(localCaps.size());
        for (const auto& seg : localCaps) {
          worldCaps.push_back({
            glm::vec3(wpnWorld * glm::vec4(seg.localP0, 1.0f)),
            glm::vec3(wpnWorld * glm::vec4(seg.localP1, 1.0f)),
            seg.radius
          });
        }
        m_player.combat->setWorldCapsules(std::move(worldCaps));
      }
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
        const auto& localCaps = m_enemy.combat->getLocalCapsules();
        if (!localCaps.empty()) {
          glm::mat4 wpnWorld = m_enemy.equipment->getWeaponTransform(forge::EquipmentComponent::RIGHT_HAND);
          std::vector<forge::WorldCapsule> worldCaps;
          worldCaps.reserve(localCaps.size());
          for (const auto& seg : localCaps) {
            worldCaps.push_back({
              glm::vec3(wpnWorld * glm::vec4(seg.localP0, 1.0f)),
              glm::vec3(wpnWorld * glm::vec4(seg.localP1, 1.0f)),
              seg.radius
            });
          }
          m_enemy.combat->setWorldCapsules(std::move(worldCaps));
        }
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
  auto inputTable = getLua().get().create_table();
  inputTable["attackLight"] = m_input.isPressed(InputAction::AttackLight);
  inputTable["attackHeavy"] = m_input.isPressed(InputAction::AttackHeavy);
  inputTable["guard"] = m_input.isHeld(InputAction::Guard);

  getLua().callFunction("onCombatUpdate", dt, inputTable);
  getCombat().update(dt);
  getLua().callFunction("onQuestUpdate", dt);

  // Bonfire interactio
  if (m_input.isPressed(InputAction::BonfireInteract)) {
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

  if (m_input.isPressed(InputAction::Interact)) {
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
  if (m_input.isPressed(InputAction::ReloadScript))
    getLua().loadScript(forge::AssetManager::getAssetRoot() + "scripts/combat/player_combat.lua");
}

void ForgeGame::onRender() {
  getRenderer().beginFrame(*m_camera, getWidth(), getHeight());
  renderScene();
  renderDebugOverlays();
  renderHUD();
  renderDeathOverlay();
  getRenderer().endFrame();
}

void ForgeGame::renderScene() {
  // Static props
  for (const auto& [model, transform] : m_staticProps)
    getRenderer().drawMesh(model, transform);

  // Draw pickups
  for (const auto& pk : m_weaponPickups) {
    if (!pk.collected && pk.model.hasRenderData())
      getRenderer().drawMesh(pk.model, pk.transform->getModelMatrix());
  }

  // Draw player weapon
  if (m_player.equipment && m_player.equipment->hasWeapon(forge::EquipmentComponent::RIGHT_HAND))
    getRenderer().drawMesh(
      m_player.weaponModel,
      m_player.equipment->getWeaponTransform(forge::EquipmentComponent::RIGHT_HAND)
    );

  // Draw enemy weapon
  if (m_enemy.active
    && m_enemy.equipment
    && m_enemy.combat->isAlive()
    && m_enemy.equipment->hasWeapon(forge::EquipmentComponent::RIGHT_HAND)) {
    getRenderer().drawMesh(
      m_enemy.weaponModel,
      m_enemy.equipment->getWeaponTransform(forge::EquipmentComponent::RIGHT_HAND)
    );
  }
  
  // Draw Native Map
  if (m_mapScene)
    getRenderer().drawBrushScene(*m_mapScene);

  // skinned characters
  getRenderer().drawSkinnedMesh(
    m_player.skinnedModel,
    m_player.transform->getModelMatrix(),
    m_player.animator->getBoneMatrices()
  );

  if (m_enemy.active) {
    forge::Transform enemyVisualTransform;
    enemyVisualTransform.setPosition(m_enemy.transform->getPosition());
    enemyVisualTransform.setScale(m_enemy.transform->getScale());
    enemyVisualTransform.setRotation(m_enemy.transform->getRotation());

    getRenderer().drawSkinnedMesh(
      m_enemy.skinnedModel,
      enemyVisualTransform.getModelMatrix(),
      m_enemy.animator->getBoneMatrices()
    );
  } 
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

      for (const auto& cap : m_player.combat->getWorldCapsules())
        forge::DebugDraw::capsulePQ(cap.worldP0, cap.worldP1, cap.radius, col);

      #ifdef SHOW_HITBOX_TRAILS
      for (int i = 0; i < m_playerTrail.count; i++) {
        int idx = (m_playerTrail.head - 1 - i + HitboxTrail::N) % HitboxTrail::N;
        float alpha = 1.0f - (float)i / (float)HitboxTrail::N;
        glm::vec3 col = { 1.0f * alpha, 0.3f * alpha, 0.0f };
        for (const auto& cap : m_playerTrail.entries[idx])
          forge::DebugDraw::capsulePQ(cap.worldP0, cap.worldP1, cap.radius, col);
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
        //forge::DebugDraw::boxOBB(
        //  m_enemy.combat->getHitboxTransform(),
        //  m_enemy.combat->getHitboxHalfExtents(),
        //  col
        //);
        #ifdef SHOW_HITBOX_TRAILS
        for (int i = 0; i < m_enemyTrail.count; i++) {
          int idx = (m_enemyTrail.head - 1 - i + HitboxTrail::N) % HitboxTrail::N;
          float alpha = 1.0f - (float)i / (float)HitboxTrail::N;
          glm::vec3 col = { 1.0f * alpha, 0.3f * alpha, 0.0f };
          for (const auto& cap : m_enemyTrail.entries[idx])
            forge::DebugDraw::capsulePQ(cap.worldP0, cap.worldP1, cap.radius, col);
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
  if (m_input.isPressed(InputAction::ToggleUIMouse)) {
    m_uiMouseMode = !m_uiMouseMode;
    glfwSetInputMode(getWindow(), GLFW_CURSOR,
                     m_uiMouseMode ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
    m_input.resetLookTracking();
    LOG_INFO("[Game] UI Mouse Mode: {}", m_uiMouseMode ? "ON" : "OFF");
  }
  if (!m_player.combat->isAlive()) {
    m_moveDir = glm::vec3(0.0f);
    m_moveSpeed = 0.0f;
    return;
  }

  if (!m_uiMouseMode && !getDebugUI().isCapturingMouse()) {
    const glm::vec2& look = m_input.getLookDelta();
    if (look.x != 0.0f || look.y != 0.0f)
      m_tpCamera->applyMouseDelta(look.x, look.y);
  }
  // Tab: Toggle lock on
  if (m_input.isPressed(InputAction::LockOn)) {
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

  bool isDodging = (m_player.animator->getCurrentStateName() == "Dodging");
  if (isDodging){
    m_player.animator->getParams().setFloat("moveSpeed", 0.0f);
    m_moveDir = glm::vec3(0.0f);
    m_moveSpeed = 0.0f;
    return;
  }

  glm::vec3 move = m_input.getWorldMoveDir();
  glm::vec3 toTarget = glm::vec3(0.0f);
  glm::vec3 strafeRight = glm::vec3(1.0f, 0.0f, 0.0f);
  if (m_lockedOn) {
    toTarget = m_lockOnEnemyPos - m_player.transform->getPosition();
    toTarget.y = 0.0f;
    if (glm::length(toTarget) > 0.001f) toTarget = glm::normalize(toTarget);
    strafeRight = glm::normalize(glm::cross(toTarget, glm::vec3(0, 1, 0)));
    move = glm::vec3(0.0f);

    if (m_input.isHeld(InputAction::MoveForward)) move += toTarget;
    if (m_input.isHeld(InputAction::MoveBack)) move -= toTarget;
    if (m_input.isHeld(InputAction::MoveRight)) move += strafeRight;
    if (m_input.isHeld(InputAction::MoveLeft)) move -= strafeRight;
  }

  // Sprint
  bool sprinting = m_input.isHeld(InputAction::Sprint);
  float speed = sprinting ? m_playerSprintSpeed : m_playerWalkSpeed; 

  // Dodge

  if (m_input.isPressed(InputAction::Dodge) && !m_player.inputLocked
      && !m_player.combat->isAttacking()) {
    glm::vec3 inputDir = m_input.getWorldMoveDir();

    DodgeDir dir = DodgeDir::Backward;
    if (glm::length(inputDir) > 0.001f) {
      if (m_lockedOn) {
        glm::vec3 lockedDir = glm::vec3(0.0f);
        if (m_input.isHeld(InputAction::MoveForward)) lockedDir += toTarget;
        if (m_input.isHeld(InputAction::MoveBack))    lockedDir -= toTarget;
        if (m_input.isHeld(InputAction::MoveRight))   lockedDir += strafeRight;
        if (m_input.isHeld(InputAction::MoveLeft))    lockedDir -= strafeRight;

        if (glm::length(lockedDir) > 0.001f) {
          lockedDir = glm::normalize(lockedDir);
          float dot = glm::dot(toTarget, lockedDir);
          float cross_y = glm::cross(lockedDir, toTarget).y;
          float angle = glm::degrees(std::atan2(cross_y, dot));

          if (angle > -22.5f && angle <= 22.5f) dir = DodgeDir::Forward;
          else if (angle > 22.5f && angle <= 67.5f) dir = DodgeDir::ForwardRight;
          else if (angle > 67.5f && angle <= 112.5f) dir = DodgeDir::Right;
          else if (angle > 112.5f && angle <= 157.5f) dir = DodgeDir::BackwardRight;
          else if (angle > 157.5f || angle <= -157.5f) dir = DodgeDir::Backward;
          else if (angle > -157.5f && angle <= -112.5f) dir = DodgeDir::BackwardLeft;
          else if (angle > -112.5f && angle <= -67.5f) dir = DodgeDir::Left;
          else dir = DodgeDir::ForwardLeft;
          LOG_TRACE("[handleInput] Set roll direction LOCKED ON, lockedDir len >0.01. (angle={},dir={})",
                    angle, (int)dir);
        }
        LOG_TRACE("[handleInput] Locked on but lockedDir <0.01: ({},{},{})", lockedDir.x, lockedDir.y, lockedDir.z);
      } else {
        dir = DodgeDir::Forward;
      } 
    }

    m_player.animator->getParams().setInt("dodgeDir", static_cast<int>(dir));

    bool isStandstill = (glm::length(inputDir) <= 0.001f);
    
    if (!isStandstill) {
      m_player.dodgeFacingQuat = glm::quatLookAt(
       -inputDir, glm::vec3(0.0f, 1.0f, 0.0f));
    } else {
      m_player.dodgeFacingQuat = m_player.transform->getRotation();
    }

    if (isStandstill)
      m_player.animator->getParams().setTrigger("backstep");
    else
      m_player.animator->getParams().setTrigger("dodge");
  }

  if (glm::length(move) > 0.001f) {
    m_moveDir = glm::normalize(move);
    m_moveSpeed = sprinting ? m_playerSprintSpeed : m_playerWalkSpeed;

    glm::vec3 faceDir = m_lockedOn
      ? (m_lockOnEnemyPos - m_player.transform->getPosition())
      : glm::vec3(move);
    faceDir.y = 0.0f;
    if (glm::length(faceDir) > 0.001f) 
      m_facingTarget = glm::normalize(faceDir);
    if (m_lockedOn) {
      float moveX = glm::dot(move, strafeRight);
      float moveZ = glm::dot(move, toTarget);
      float spd = m_input.getMoveSpeed();
      m_player.animator->getParams().setFloat("moveX", moveX * spd);
      m_player.animator->getParams().setFloat("moveZ", moveZ * spd);
      m_player.animator->getParams().setBool("isLockedOn", true);
    } else {
      float moveSpeedParam = sprinting ? 2.0f : 1.0f;
      m_player.animator->getParams().setFloat("moveSpeed", moveSpeedParam);
      m_player.animator->getParams().setBool("isLockedOn", false);
    }
  } else {
    m_moveDir = glm::vec3(0.0f);
    m_moveSpeed = 0.0f;
    if (m_lockedOn) {
      m_player.animator->getParams().setFloat("moveX", 0.0f);
      m_player.animator->getParams().setFloat("moveZ", 0.0f);
      m_player.animator->getParams().setBool("isLockedOn", true);
    } else {
      m_player.animator->getParams().setFloat("moveSpeed", 0.0f);
      m_player.animator->getParams().setBool("isLockedOn", false);
    }
    if (m_lockedOn) {
      glm::vec3 faceDir = m_lockOnEnemyPos - m_player.transform->getPosition();
      faceDir.y = 0.0f;
      if (glm::length(faceDir) > 0.001f)
        m_player.forward = glm::normalize(faceDir);
    }
  }
}

void ForgeGame::applyMovement(float dt) {
  if (!m_player.combat->isAlive()) {
    m_player.controller->setWalkDirection(glm::vec3(0.0f));
    return;
  }

  glm::vec3 displacement(0.0f);

  bool isDodging = (m_player.animator->getCurrentStateName() == "Dodging");
  bool isBackstepping = (m_player.animator->getCurrentStateName() == "Backstep");
  bool isLocomotion = (m_player.animator->getCurrentStateName() == "Locomotion");
  //bool useRM = (isLocomotion && m_player.animator->isRootMotionActive()) || m_rmOverride;
  bool useRM = (isLocomotion && m_player.animator->isRootMotionActive())
              || m_rmOverride || isDodging || isBackstepping;

  if (useRM) {
    glm::vec3 localDelta = m_player.animator->getRootMotionDelta();
    localDelta *= m_player.transform->getScale().x;

    if (m_rmOverride) {
      if (!m_rmAxes.x) localDelta.x = 0.0f;
      if (!m_rmAxes.y) localDelta.y = 0.0f;
      if (!m_rmAxes.z) localDelta.z = 0.0f;
      localDelta *= m_rmScale;
    }

    if (isDodging || isBackstepping) {
      m_player.transform->setRotation(m_player.dodgeFacingQuat);
      m_player.forward = m_player.dodgeFacingQuat * glm::vec3(0.0f, 0.0f, -1.0f);
    }
    glm::quat facing = m_player.transform->getRotation();
    displacement = facing * localDelta * (forge::PhysicsWorld::k_fixedStep / dt);
  } else {
    displacement = m_moveDir * m_moveSpeed * forge::PhysicsWorld::k_fixedStep;
  }
  
  if (!isDodging && !isBackstepping) {
    float rate = m_lockedOn ? k_lockOnTurnRate : k_turnRateDeg;
    glm::vec3 tgt = m_facingTarget;
    // build target rotation: models rest forward is -z so negate fwd to get the look dir
    glm::quat targetRot = glm::quatLookAt(-tgt, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::quat currentRot = m_player.transform->getRotation();

    float angle = glm::angle(glm::inverse(currentRot) * targetRot);
    float maxAngle = glm::radians(rate * dt);
    float t = (angle > 0.001f) ? glm::min(1.0f, maxAngle / angle) : 1.0f;

    glm::quat newRot = glm::slerp(currentRot, targetRot, t);
    m_player.transform->setRotation(newRot);
    // Keep m+playe.forward in sync with the actual slerped rotation
    m_player.forward = newRot * glm::vec3(0.0f, 0.0f, -1.0f);
  }

  m_player.controller->setWalkDirection(displacement);
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

