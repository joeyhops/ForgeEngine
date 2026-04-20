#pragma once
#include <forge/LevelLoader.h>
#include <forge/map/MapGeometryTypes.h>
#include <forge/map/MapScene.h>
#include <forge/RigidBodyComponent.h>
#include <forge/TriggerVolume.h>

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include <vector>

namespace forge {

class PhysicsWorld;
class LuaState;

// A fully assembled entity
struct EntityInstance {
  std::vector<MapRenderObject> renderObjects;
  std::unique_ptr<RigidBodyComponent> rigidBody;
  std::unique_ptr<TriggerVolume> trigger;

  std::string classname;
  std::string targetname;
  std::string target;
  glm::vec3 origin = {0, 0, 0};
  std::unordered_map<std::string, std::string> props;
};

using EntityFactory = std::function<EntityInstance(
  const LevelEntity& ent,
  const EntityGeometry& geom,
  PhysicsWorld& physics,
  LuaState& lua
)>;

class EntityAssembler {
public:
  void registerFactory(const std::string& classname, EntityFactory f);
  bool hasFactory(const std::string& classname) const;

  // Executes factory and populates the base properties
  EntityInstance assemble(const LevelEntity& e, const EntityGeometry& geom, PhysicsWorld& physics, LuaState& lua) const;
private:
  std::unordered_map<std::string, EntityFactory> m_factories;
};

}
