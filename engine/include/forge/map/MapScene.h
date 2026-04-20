#pragma once
#include <forge/Mesh.h>
#include <forge/Shader.h>
#include <forge/Texture.h>
#include <forge/Camera.h>

#include <memory>
#include <vector>

namespace forge {

struct MapRenderObject {
  std::shared_ptr<Mesh> mesh;
  std::shared_ptr<Texture> albedo;
  std::shared_ptr<Texture> normalMap; // can be nullptr
  std::shared_ptr<Texture> roughnessMap; // can be nullptr
  std::shared_ptr<Texture> metallicMap; // can be nullptr
  std::shared_ptr<Texture> aoMap; // can be nullptr
};

class MapScene {
public:
  std::vector<MapRenderObject> renderObjects;
  std::vector<glm::vec3> collisionPositions;
  std::vector<uint32_t> collisionIndices;

  void render(const Camera& camera, Shader* brushShader) const;
};

}
