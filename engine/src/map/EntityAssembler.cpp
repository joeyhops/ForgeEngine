#include <forge/map/EntityAssembler.h>
#include <forge/Logger.h>

namespace forge {

void EntityAssembler::registerFactory(const std::string& classname, EntityFactory f) {
  if (m_factories.count(classname))
    LOG_WARN("[EntityAssembler] Overwriting Entity Factory for classname '{}'", classname);
  m_factories[classname] = std::move(f);
}

bool EntityAssembler::hasFactory(const std::string& classname) const {
  return m_factories.find(classname) != m_factories.end();
}

EntityInstance EntityAssembler::assemble(const LevelEntity& e,
                                         const EntityGeometry& geom,
                                         PhysicsWorld& physics,
                                         LuaState& lua) const {
  auto it = m_factories.find(e.classname);
  if (it != m_factories.end()) {
    EntityInstance inst = it->second(e, geom, physics, lua);
    //automatically populate base KV properties so factories don't have to
    inst.classname = e.classname;
    inst.origin = e.origin;
    inst.targetname = e.getProperty("targetname");
    inst.target = e.getProperty("target");
    inst.props = e.props;
    return inst;
  }

  EntityInstance inst;
  inst.classname = e.classname;
  return inst;
}

}
