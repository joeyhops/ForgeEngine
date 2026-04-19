#include <forge/FlagManager.h>
#include <forge/EventBus.h>
#include <forge/Events.h>
#include <forge/Logger.h>

#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>

namespace forge {

void FlagManager::set(uint32_t id, bool value) {
  bool current = get(id);
  if (current == value) return; // No change - no event
  
  if (value)
    m_flags[id] = true;
  else
    m_flags.erase(id); // False flags aren't stored
  
  LOG_TRACE("[Flags] Flag {} -> {}", id, value ? "true" : "false");
  EventBus::publish(FlagChangeEvent{ .flagId = id, .newValue = value });
}

bool FlagManager::get(uint32_t id) const {
  auto it = m_flags.find(id);
  return (it != m_flags.end()) && it->second;
}

bool FlagManager::toggle(uint32_t id) {
  bool newVal = !get(id);
  set(id, newVal);
  return newVal;
}

// Save / Load

void FlagManager::saveToFile(const std::string& path) const {
  nlohmann::json j;

  // Only serialize true flags - false is default
  auto& flagsJson = j["flags"];
  for (const auto& [id, val] : m_flags) {
    if (val)
      flagsJson[std::to_string(id)] = true;
  }

  auto parent = std::filesystem::path(path).parent_path();
  if (!parent.empty())
    std::filesystem::create_directories(parent);

  std::ofstream file(path);
  if (!file.is_open())
    throw std::runtime_error("Cannot write save file: " + path);

  file << j.dump(4); //pretty print with4 space indent
  LOG_INFO("[Flags] Saved {} flags to '{}'", m_flags.size(), path);
}

void FlagManager::loadFromFile(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    LOG_WARN("[Flags] No save file at '{}' - starting fresh", path);
    return;
  }

  nlohmann::json j;
  try {
    file >> j;
  } catch (const nlohmann::json::exception& e) {
    throw std::runtime_error("Corrupt save file: " + std::string(e.what()));
  }

  m_flags.clear();
  if (j.contains("flags")) {
    for (auto& [key, val] : j["flags"].items()) {
      uint32_t id = static_cast<uint32_t>(std::stoul(key));
      m_flags[id] = val.get<bool>();
    }
  }

  LOG_INFO("[Flags] Loaded {} flags from '{}'", m_flags.size(), path);
}

}
