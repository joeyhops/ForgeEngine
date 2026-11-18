#include "support/GlmMatchers.h"
#include "support/TestPaths.h"

#include <forge/LevelLoader.h>
#include <forge/map/GeometryGenerator.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace forge;
using namespace forge::test;
using Catch::Matchers::WithinAbs;

namespace {
// TB box x [-576,-448], y [-64,64], z [-83,-51]; engine = (x, z, -y) / 64
const glm::vec3 kMin = { -9.0f, -1.296875f, -1.0f };
const glm::vec3 kMax = { -7.0f, -0.796875f,  1.0f };
const glm::vec3 kCenter = (kMin + kMax) * 0.5f;
}

TEST_CASE("LevelLoader parses entities, key/values and brush faces",
          "[component][map]") {
  const LevelData level = LevelLoader::load(dataFile("maps/one_box.map"));
  REQUIRE(level.valid);
  REQUIRE(level.entities.size() == 2);

  const LevelEntity* world = level.findFirst("worldspawn");
  REQUIRE(world != nullptr);
  REQUIRE(world->brushes.size() == 1);
  CHECK(world->brushes[0].faces.size() == 6);
  CHECK(world->brushes[0].faces[0].textureName == "__TB_empty");
  CHECK(world->brushes[0].faces[0].uvFormat == UVFormat::Standard);

  const LevelEntity* spawn = level.findFirst("enemy_spawn");
  REQUIRE(spawn != nullptr);
  CHECK_THAT(spawn->origin, Near({ -11.0390625f, -0.609375f, -2.8828125f }, 1e-6f));
  CHECK(spawn->angle == 90.0f);
  CHECK(spawn->getProperty("weaponId") == "dagger");
  CHECK(spawn->getInt("respawns", 7) == 0);
  CHECK_FALSE(spawn->getBool("respawns", true));
  CHECK(spawn->getFloat("missing", 2.5f) == 2.5f);
  CHECK(level.getByClass("patrol_waypoint").empty());
}

TEST_CASE("LevelLoader reports an unreadable map as invalid",
          "[component][map]") {
  const LevelData level = LevelLoader::load(dataFile("maps/does_not_exist.map"));
  CHECK_FALSE(level.valid);
  CHECK(level.entities.empty());
}

TEST_CASE("GeometryGenerator turns a box brush into a closed, outward-facing mesh",
          "[component][map]") {
  const LevelData level = LevelLoader::load(dataFile("maps/one_box.map"));
  const LevelEntity* world = level.findFirst("worldspawn");
  REQUIRE(world != nullptr);

  GeometryGenerator gen;
  const EntityGeometry geo = gen.processEntity(*world);

  CHECK(geo.collisionPositions.size() == 24);
  CHECK(geo.collisionIndices.size() == 36);
  REQUIRE(geo.surfaces.size() == 1);
  const SurfaceData& surf = geo.surfaces[0];
  CHECK(surf.textureName == "__TB_empty");
  REQUIRE(surf.vertices.size() == 24);
  REQUIRE(surf.indices.size() == 36);

  for (const auto& p : geo.collisionPositions) {
    CHECK(p.x >= kMin.x - 1e-4f); CHECK(p.x <= kMax.x + 1e-4f);
    CHECK(p.y >= kMin.y - 1e-4f); CHECK(p.y <= kMax.y + 1e-4f);
    CHECK(p.z >= kMin.z - 1e-4f); CHECK(p.z <= kMax.z + 1e-4f);
  }

  for (const auto& v : surf.vertices) {
    CHECK_THAT(glm::length(v.normal), WithinAbs(1.0, 1e-5));
    CHECK(glm::dot(v.normal, v.position - kCenter) > 0.0f);
  }

  for (size_t i = 0; i + 2 < surf.indices.size(); i += 3) {
    const MapVertex& a = surf.vertices[surf.indices[i]];
    const MapVertex& b = surf.vertices[surf.indices[i + 1]];
    const MapVertex& c = surf.vertices[surf.indices[i + 2]];
    const glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
    INFO("triangle " << i / 3);
    CHECK(glm::length(n) > 1e-6f);
    // CCW seen from outside, OpenGL's default front face
    CHECK(glm::dot(n, a.normal) > 0.0f);
  }
}

TEST_CASE("GeometryGenerator skips brushes with fewer than four faces",
          "[component][map]") {
  LevelEntity ent;
  ent.classname = "worldspawn";
  ent.brushes.push_back(BrushData{ { FaceData{}, FaceData{}, FaceData{} } });
  GeometryGenerator gen;
  const EntityGeometry geo = gen.processEntity(ent);
  CHECK(geo.collisionPositions.empty());
  CHECK(geo.surfaces.empty());
}
