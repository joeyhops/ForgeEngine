#pragma once
#include <glm/glm.hpp>

namespace forge {

struct Socket {
  int boneIndex = -1;
  glm::mat4 localOffset = glm::mat4(1.0);
};

}
