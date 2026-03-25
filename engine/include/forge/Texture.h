#pragma once
#include <string>

namespace forge {

struct Texture {
  unsigned int id = 0;
  int width = 0;
  int height = 0;
  std::string path;

  // Non-copyable - owns a GPU texture
  Texture() = default;
  ~Texture();
  Texture(const Texture&) = delete;
  Texture& operator=(const Texture&) = delete;

  void bind(unsigned int unit = 0) const;
};

}
