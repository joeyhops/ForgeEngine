#include <forge/map/GeometryGenerator.h>
#include <forge/Logger.h>

#include <unordered_map>
#include <algorithm>

namespace forge {

// Base axis table for Standard UV Projection (used as a fallback)
// Arranged as { faceNormal, U-axis, V-axis }
static const glm::vec3 baseaxis[18] = {
  {0,0,1}, {1,0,0}, {0,-1,0}, // floor (Z+)
  {0,0,-1}, {1,0,0}, {0,-1,0}, // ceiling (Z-)
  {1,0,0}, {0,1,0}, {0,0,-1}, // west wall 
  {-1,0,0}, {0,1,0}, {0,0,-1}, // east wall
  {0,1,0}, {1,0,0}, {0,0,-1}, // south wall
  {0,-1,0}, {1,0,0}, {0,0,-1}, // north wall
};

GeometryGenerator::Plane GeometryGenerator::planeFromFace(const FaceData& face) {
  // Normal points outward from the brush interior
  glm::vec3 n = glm::normalize(glm::cross(
    face.planePoints[2] - face.planePoints[0],
    face.planePoints[1] - face.planePoints[0]
  ));
  return { n, glm::dot(n, face.planePoints[0]) };
}

bool GeometryGenerator::intersectThreePlanes(const Plane& p1, const Plane& p2, const Plane& p3, glm::vec3& outPoint) {
  glm::vec3 c23 = glm::cross(p2.normal, p3.normal);
  glm::vec3 c31 = glm::cross(p3.normal, p1.normal);
  glm::vec3 c12 = glm::cross(p1.normal, p2.normal);

  float denom = glm::dot(p1.normal, c23);
  // if denominator is near zero, planes are parallel/degenerate
  if (std::abs(denom) < 1e-6f) return false;

  outPoint = (p1.dist * c23 + p2.dist * c31 + p3.dist * c12) / denom;
  return true;
}

bool GeometryGenerator::pointInsideAllPlanes(const glm::vec3& pt, const std::vector<Plane>& planes, float epsilon) {
  // A point is "inside" if it is behind or exactly on every plane bounding the brush
  for (const auto& p : planes) {
    if (glm::dot(pt, p.normal) - p.dist > epsilon) {
      return false;
    }
  }
  return true;
}

void GeometryGenerator::windFaceVertices(std::vector<glm::vec3>& verts, const glm::vec3& faceNormal) {
  if (verts.size() < 3) return;

  // 1. Find the centroid of the face
  glm::vec3 centroid(0.0f);
  for (const auto& v : verts) {
    centroid += v;
  }
  centroid /= static_cast<float>(verts.size());

  // 2. Create a local 2D coord system on the face plane
  glm::vec3 uAxis = glm::normalize(verts[0] - centroid);
  glm::vec3 vAxis = glm::cross(faceNormal, uAxis);

  // 3. Sort vertices by their angle around the centroid (Counter-Clockwise)
  std::sort(verts.begin(), verts.end(), [&](const glm::vec3& a, const glm::vec3& b) {
    glm::vec3 da = a - centroid;
    glm::vec3 db = b - centroid;
    return std::atan2(glm::dot(da, vAxis), glm::dot(da, uAxis)) <
           std::atan2(glm::dot(db, vAxis), glm::dot(db, uAxis));
  });
}

void GeometryGenerator::computeValve220UVs(const FaceData& face,
                                           const glm::vec3& vertex,
                                           const GeometrySettings& settings,
                                           glm::vec2& outUV,
                                           glm::vec4& outTangent) {
  // Scale vertex back up to TB units for UVMapping
  glm::vec3 quakeVert = vertex * 64.0f;

  // Valve 220 explicit axes
  float u = (glm::dot(quakeVert, face.uAxis) / face.uScale + face.uOffset) / settings.defaultTexWidth;
  float v = (glm::dot(quakeVert, face.vAxis) / face.vScale + face.vOffset) / settings.defaultTexHeight;
  outUV = { u, v };

  // U-axis is the tangent vector, normalize it. W component = 1.0
  outTangent = glm::vec4(glm::normalize(face.uAxis), 1.0f);
}


void GeometryGenerator::computeStandardUVs(const FaceData& face,
                               const glm::vec3& vertex,
                               const glm::vec3& faceNormal,
                               const GeometrySettings& settings,
                               glm::vec2& outUV,
                               glm::vec4& outTangent) {
  int bestAxis = 0;
  float bestDot = -9999.0f;
  for (int i = 0; i < 6; i++) {
    float d = glm::dot(faceNormal, baseaxis[i * 3]);
    if (d > bestDot) {
      bestDot = d;
      bestAxis = i;
    }
  }

  glm::vec3 uAxis = baseaxis[bestAxis * 3 + 1];
  glm::vec3 vAxis = baseaxis[bestAxis * 3 + 2];

  // Apply rotation
  if (face.rotation != 0.0f) {
    float rad = glm::radians(face.rotation);
    float c = std::cos(rad), s = std::sin(rad);
    glm::vec3 tempU = uAxis * c - vAxis * s;
    glm::vec3 tempV = uAxis * s + vAxis * c;
    uAxis = tempU;
    vAxis = tempV;
  }
  // Scale vertex back up to TB units for UVMapping
  glm::vec3 quakeVert = vertex * 64.0f;

  float u = (glm::dot(quakeVert, uAxis) / face.xScale + face.xOffset) / settings.defaultTexWidth;
  float v = (glm::dot(quakeVert, vAxis) / face.yScale + face.yOffset) / settings.defaultTexHeight;
  outUV = { u, v };
  outTangent = glm::vec4(glm::normalize(uAxis), 1.0f);
}

EntityGeometry GeometryGenerator::processEntity(const LevelEntity& entity, const GeometrySettings& settings) {
  EntityGeometry result;
  std::unordered_map<std::string, SurfaceData> surfaceMap;

  for (const auto& brush : entity.brushes) {
    int numFaces = static_cast<int>(brush.faces.size());
    if (numFaces < 4) continue; // min bounding planes for closed convex volume

    std::vector<Plane> planes;
    planes.reserve(numFaces);
    for (const auto& face : brush.faces) {
      planes.push_back(planeFromFace(face));
    }

    // Accumulate valid intersection points per face
    std::vector<std::vector<glm::vec3>> faceVertices(numFaces);

    // Triple plane int. algo O(N^3)
    for (int i = 0; i < numFaces - 2; i++) {
      for (int j = i + 1; j < numFaces - 1; j++) {
        for (int k = j + 1; k < numFaces; k++) {
          glm::vec3 pt;
          if (!intersectThreePlanes(planes[i], planes[j], planes[k], pt)) {
            continue;
          }

          // Is point bounded by other planes?
          if (!pointInsideAllPlanes(pt, planes)) {
            continue;
          }

          // Prevent dupe verts on same face
          auto addUnique = [](std::vector<glm::vec3>& list, const glm::vec3& p) {
            for (const auto& existing : list) {
              if (glm::distance(existing, p) < 1e-4f) return;
            }
            list.push_back(p);
          };

          addUnique(faceVertices[i], pt);
          addUnique(faceVertices[j], pt);
          addUnique(faceVertices[k], pt);
        }
      }
    }

    // Triangulate and route to appropriate output buffers
    for (int i = 0; i < numFaces; i++) {
      const FaceData& face = brush.faces[i];
      auto& verts = faceVertices[i];

      if (verts.size() < 3) continue; // Face got degenerate or clipped completely

      windFaceVertices(verts, planes[i].normal);

      // Special texture handling
      if (settings.isSkip(face.textureName)) continue; // discard

      bool isClip = settings.isClip(face.textureName);
      bool isTrigger = settings.isTrigger(face.textureName);
      bool isNoDraw = settings.isNoDraw(face.textureName);

      // Unless texture is Trigger or skip, always add collision
      if (!isTrigger) {
        uint32_t colBase = static_cast<uint32_t>(result.collisionPositions.size());
        for (const auto& v : verts) {
          result.collisionPositions.push_back(v);
        }

        // Fan triangulation
        for (size_t j = 1; j + 1 < verts.size(); j++) {
          result.collisionIndices.push_back(colBase + 0);
          result.collisionIndices.push_back(colBase + static_cast<uint32_t>(j));
          result.collisionIndices.push_back(colBase + static_cast<uint32_t>(j + 1));
        }
      }

      // Exclude non-rendered surfaces from render geometry
      if (isClip || isTrigger || isNoDraw) continue;

      // Gen render verts
      std::vector<MapVertex> renderVerts;
      renderVerts.reserve(verts.size());

      for (const auto& v : verts) {
        MapVertex mv;
        mv.position = v;
        mv.normal = planes[i].normal;

        if (face.uvFormat == UVFormat::Valve220) {
          computeValve220UVs(face, v, settings, mv.texCoord, mv.tangent);
        } else {
          computeStandardUVs(face, v, planes[i].normal, settings, mv.texCoord, mv.tangent);
        }

        renderVerts.push_back(mv);
      }

      // Gen render idx (Fan triangulation)
      std::vector<uint32_t> renderIndices;
      for (size_t j = 1; j + 1 < verts.size(); j++) {
        renderIndices.push_back(0);
        renderIndices.push_back(static_cast<uint32_t>(j));
        renderIndices.push_back(static_cast<uint32_t>(j + 1));
      }

      if (surfaceMap.find(face.textureName) == surfaceMap.end()) {
        surfaceMap[face.textureName].textureName = face.textureName;
      }
      surfaceMap[face.textureName].addFace(renderVerts, renderIndices);
    }
  }

  // Flatten map -> vector
  for (auto& [tex, surf] : surfaceMap) {
    result.surfaces.push_back(std::move(surf));
  }

  return result;
}

}
