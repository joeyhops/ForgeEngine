#pragma once
#include <forge/LevelLoader.h>
#include <forge/map/MapGeometryTypes.h>
#include <forge/map/GeometrySettings.h>

namespace forge {

class GeometryGenerator {
public:
  // Transforms parsed BrushData into triangulated EntityGeometry
  EntityGeometry processEntity(const LevelEntity& entity, const GeometrySettings& settings = {});

private:
  struct Plane {
    glm::vec3 normal;
    float dist;
  };

  static Plane planeFromFace(const FaceData& face);
  static bool intersectThreePlanes(const Plane& p1, const Plane& p2, const Plane& p3, glm::vec3& outPoint);
  static bool pointInsideAllPlanes(const glm::vec3& pt, const std::vector<Plane>& planes, float epsilon = 1e-3f);
  static void windFaceVertices(std::vector<glm::vec3>& verts, const glm::vec3& faceNormal);

  // UV Mapping helpers
  static void computeValve220UVs(const FaceData& face,
                                 const glm::vec3& vertex,
                                 const GeometrySettings& settings,
                                 glm::vec2& outUV,
                                 glm::vec4& outTangent);
  static void computeStandardUVs(const FaceData& face,
                                 const glm::vec3& vertex,
                                 const glm::vec3& faceNormal,
                                 const GeometrySettings& settings,
                                 glm::vec2& outUV,
                                 glm::vec4& outTangent);
};

}
