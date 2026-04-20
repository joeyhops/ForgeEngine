#pragma once
#include <string>
#include <vector>
#include <algorithm>

namespace forge {

struct GeometrySettings {
  // Normalize UVs when actual texture isn't loaded yet
  // 128 is the std
  float defaultTexWidth = 128.0f;
  float defaultTexHeight = 128.0f;

  // Handle special textures
  std::vector<std::string> skipTextures = {"skip", "hint", "hintskip"};
  std::vector<std::string> clipTextures = {"clip"};
  std::vector<std::string> triggerTextures = {"trigger"};
  std::vector<std::string> nodrawTextures = {"nodraw"};

  bool isSkip(const std::string& tex) const {
    return std::find(skipTextures.begin(), skipTextures.end(), tex) != skipTextures.end();
  }

  bool isClip(const std::string& tex) const {
    return std::find(clipTextures.begin(), clipTextures.end(), tex) != clipTextures.end();
  }

  bool isTrigger(const std::string& tex) const {
    return std::find(triggerTextures.begin(), triggerTextures.end(), tex) != triggerTextures.end();
  }

  bool isNoDraw(const std::string& tex) const {
    return std::find(nodrawTextures.begin(), nodrawTextures.end(), tex) != nodrawTextures.end();
  }

};

}
