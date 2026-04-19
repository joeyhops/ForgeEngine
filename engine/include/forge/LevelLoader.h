#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>

namespace forge {

enum class UVFormat { Standard, Valve220 };

struct FaceData {
  glm::vec3 planePoints[3];
  std::string textureName;
  UVFormat uvFormat = UVFormat::Standard;

  //std UV
  float xOffset = 0, yOffset = 0, rotation = 0, xScale = 1, yScale = 1;

  // Valve220 UV
  glm::vec3 uAxis = {1,0,0}; float uOffset = 0; float uScale = 1;
  glm::vec3 vAxis = {0,1,0}; float vOffset = 0; float vScale = 1;
};

struct BrushData {
  std::vector<FaceData> faces;
};

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

  std::vector<BrushData> brushes;

  std::string getProperty(const std::string& key, const std::string& def = "") const;
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
