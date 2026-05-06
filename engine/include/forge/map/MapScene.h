#pragma once
#include <forge/Mesh.h>
#include <forge/Material.h>

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace forge {

struct MapRenderObject {
  std::shared_ptr<Mesh> mesh;
  Material material;
};

class MapScene {
public:
  std::vector<MapRenderObject> renderObjects;
  std::vector<glm::vec3> collisionPositions;
  std::vector<uint32_t> collisionIndices;
};

}
