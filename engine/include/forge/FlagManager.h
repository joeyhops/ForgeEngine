#pragma once
#include <unordered_map>
#include <string>
#include <cstdint>

namespace forge {
  
class FlagManager {
public:
  FlagManager() = default;
  ~FlagManager() = default;

  // Set a flag and fire FlagChangedEvent if the value changed
  void set(uint32_t id, bool value);

  // Get a flag - Returns false if never set (default world state)
  bool get(uint32_t id) const;

  // Toggle a flag, returns the new value
  bool toggle(uint32_t id);

  //Serialization -- called on save/load
  void saveToFile(const std::string& path) const;
  void loadFromFile(const std::string& path);

  // Reset all flags
  void clear() { m_flags.clear(); }

  // How many flags are set (useful for debug UI)
  size_t count() const { return m_flags.size(); }

  const std::unordered_map<uint32_t, bool>& getAll() const { return m_flags; }
private:
  //Only store flags that are true - false is the default
  //This keeps save files small even with thousands of flag IDs
  std::unordered_map<uint32_t, bool> m_flags;
};
}
