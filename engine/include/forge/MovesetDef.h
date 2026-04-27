#pragma once
#include <string>
#include <unordered_map>

namespace forge {

struct MovesetDef {
  std::string id;
  std::unordered_map<std::string, std::string> clips;
};

}
