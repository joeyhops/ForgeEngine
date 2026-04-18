#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>

namespace forge {

// LevelEntity
// Represents a single point entity parsed from Quak-format .map file.
// Brush geometry ignored entirely, only kv pairs captured
//

struct LevelEntity {
  std::string classname;
  glm::vec3 origin = { 0.0f, 0.0f, 0.0f };
  float angle = 0.0f;

  // All other key/value pairs from the entity block, excluding classname or position data
  std::unordered_map<std::string, std::string> props;

  int getInt(const std::string& key, int def = 0) const;
  float getFloat(const std::string& key, float def = 0.0f) const;
  bool getBool(const std::string& key, bool def = false) const;
};

// LevelData
struct LevelData {
  std::vector<LevelEntity> entities;
  bool valid = false; // false if the .map file could not be opened

  // returns every entity whos classname matches. Empty if none.
  std::vector<const LevelEntity*> getByClass(const std::string& classname) const;

  // Returns first matching entity or nullptr if none
  const LevelEntity* findFirst(const std::string& classname) const;
};

// LevelLoader
//
// Parses the entity section of the Quake 2 format .map file.
//
// Coordinate conversion applied automatically:
//    engine.x = map.x * mapScale
//    engine.y = map.z * mapScale (Z-up -> Y-up)
//    engine.z = -map.y * mapScale
//
// default mapscale matches recommended TrenchBroom convention (64TB Units = 1 engine meter)

class LevelLoader {
public:
  static constexpr float k_defaultMapScale = 1.0f / 64.0f;

  // Loads and parses entity data from .map file.
  static LevelData load(const std::string& absPath, float mapScale = k_defaultMapScale);
private:
  static glm::vec3 parseOrigin(const std::string& value, float mapScale);

  static bool extractToken(const std::string& line, size_t startPos, std::string& outToken, size_t& outEnd);

};
}
