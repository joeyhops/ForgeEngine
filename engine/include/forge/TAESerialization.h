#pragma once
#include <forge/AnimationClip.h>
#include <string>
#include <vector>

namespace forge {

class TAESerialization {
public:
  static std::vector<AnimEvent> load(const std::string& absPath,
                                     std::string& outRootBone,
                                     bool& outExtractY);

  static std::vector<AnimEvent> load(const std::string& absPath);

  static void save(const std::string& absPath, const AnimationClip& clip);

};

}
