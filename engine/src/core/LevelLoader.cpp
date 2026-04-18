#include <forge/LevelLoader.h>
#include <forge/Logger.h>

#include <fstream>
#include <sstream>
#include <string>
#include <cctype>
#include <stdexcept>

namespace forge {
  
int LevelEntity::getInt(const std::string& key, int def) const {
  auto it = props.find(key);
  if (it == props.end()) return def;
  try { return std::stoi(it->second); }
  catch (...) { return def; }
}

float LevelEntity::getFloat(const std::string& key, float def) const {
  auto it = props.find(key);
  if (it == props.end()) return def;
  try { return std::stof(it->second); }
  catch (...) { return def; }
}

bool LevelEntity::getBool(const std::string& key, bool def) const {
  auto it = props.find(key);
  if (it == props.end()) return def;
  const std::string& v = it->second;
  if (v == "1" || v == "true" || v == "yes") return true;
  if (v == "0" || v == "false" || v == "no") return false;
  return def;
}

std::vector<const LevelEntity*> LevelData::getByClass(const std::string& classname) const {
  std::vector<const LevelEntity*> result;
  for (const auto& e : entities) {
    if (e.classname == classname)
      result.push_back(&e);
  }
  return result;
}

const LevelEntity* LevelData::findFirst(const std::string& classname) const {
  for (const auto& e : entities) {
    if (e.classname == classname)
      return &e;
  }
  return nullptr;
}

// Helpers

// Convert .map Quake origin: Z-up, Y-forward
// to engines Y-up, Z-backward (OpenGL) convention
//
// Conversion:
//    engine.x = map.x * scale
//    engine.y = map.z * scale <- Z becomes Y
//    engine.z = -map.y * scale <- negate Y to flip forward axis
glm::vec3 LevelLoader::parseOrigin(const std::string& value, float mapScale) {
  std::istringstream ss(value);
  float mx = 0.0f, my = 0.0f, mz = 0.0f;
  ss >> mx >> my >> mz;

  return glm::vec3(
    mx * mapScale,
    mz * mapScale, // engine Y = map Z (up)
    -my * mapScale
  );
}

bool LevelLoader::extractToken(const std::string& line, size_t startPos, std::string& outToken, size_t& outEnd) {
  size_t i = startPos;

  // Skip whitespace
  while (i < line.size() && std::isspace((unsigned char)line[i])) ++i;

  if (i >= line.size() || line[i] != '"') return false;
  ++i; // consume opening quote

  size_t tokenStart = i;
  while (i < line.size() && line[i] != '"') ++i;

  if (i >= line.size()) return false; // Unterminated quote

  outToken = line.substr(tokenStart, i - tokenStart);
  outEnd = i + 1; // One past final quote
  return true;
}

LevelData LevelLoader::load(const std::string& absPath, float mapScale) {
  LevelData result;

  std::ifstream file(absPath);
  if (!file.is_open()) {
    LOG_WARN("[LevelLoader] Could not open: {}", absPath);
    return result;
  }

  LOG_INFO("[LevelLoader] Parsing: {}", absPath);

  std::string line;
  int entityCount = 0;
  int skippedBrush = 0;

  while (std::getline(file, line)) {
    if (line.empty()) continue;
    size_t firstNonSpace = line.find_first_not_of(" \t\r\n");
    if (firstNonSpace == std::string::npos) continue;
    if (line[firstNonSpace] == '/' && line.size() > firstNonSpace + 1 && line[firstNonSpace + 1] == '/')
      continue;

    // Entities begin with top-level '{'
    if (line[firstNonSpace] != '{') continue;

    // Read entity
    LevelEntity ent;
    int brushDepth = 0;

    while (std::getline(file, line)) {
      size_t ns = line.find_first_not_of(" \t\r\n");
      if (ns == std::string::npos) continue;

      char first = line[ns];

      // Skip comments
      if (first == '/' && line.size() > ns + 1 && line[ns + 1] == '/')
        continue;

      // open brace (could be geometry brush)
      if (first == '{') {
        ++brushDepth;
        continue;
      }

      // close brace
      if (first == '}') {
        if (brushDepth > 0) {
          --brushDepth;
          ++skippedBrush;
          continue;
        }
        break;
      }

      // Skip geometrey
      if (brushDepth > 0) continue;

      // KV-pair
      if (first != '"') continue; // Not kv line

      std::string key, value;
      size_t afterKey = 0;
      if (!extractToken(line, ns, key, afterKey)) continue;

      size_t afterValue = 0;
      if (!extractToken(line, afterKey, value, afterValue)) continue;

      // Demux well-known fields else everything goes into props
      if (key == "classname") {
        ent.classname = value;
      } else if (key == "origin") {
        ent.origin = parseOrigin(value, mapScale);
      } else if (key == "angle") {
        try { ent.angle = std::stof(value); }
        catch (...) {}
      } else {
        ent.props[key] = value;
      }
    }

    if (ent.classname == "worldspawn") continue;
    if (ent.classname.empty()) continue;

    result.entities.push_back(std::move(ent));
    ++entityCount;
  }

  result.valid = true;

  LOG_INFO("[LevelLoader] Parsed {} entities ({} brush blocks skipped) from '{}'",
           entityCount, skippedBrush, absPath);

  return result;
}

}
