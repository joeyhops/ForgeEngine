#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace forge {

// CPU-side vertices for map geometry
struct MapVertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 texCoord;
  glm::vec4 tangent;
};

// Holds all triangulated geometry for a single texture within an entity
struct SurfaceData {
  std::string textureName;
  std::vector<MapVertex> vertices;
  std::vector<uint32_t> indices;

  void addFace(const std::vector<MapVertex>& faceVerts, const std::vector<uint32_t>& faceIndices) {
    uint32_t baseIdx = static_cast<uint32_t>(vertices.size());
    vertices.insert(vertices.end(), faceVerts.begin(), faceVerts.end());
    for (uint32_t idx : faceIndices) {
      indices.push_back(baseIdx + idx);
    }
  }
};

// Complete output for a single LevelEntity
struct EntityGeometry {
  std::vector<SurfaceData> surfaces;
  std::vector<glm::vec3> collisionPositions;
  std::vector<uint32_t> collisionIndices;
};

}
