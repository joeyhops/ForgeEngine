#pragma once

#include <forge/AnimationClip.h>
#include <forge/SyncTrack.h>

#include <string>
#include <vector>

namespace forge {

class TAESerialization {
public:
  static std::vector<AnimEvent> load(const std::string& absPath,
                                     std::string& outRootBone,
                                     bool& outExtractY,
                                     SyncTrack& outSyncTrack);
  
  static std::vector<AnimEvent> load(const std::string& absPath,
                                     std::string& outRootBone,
                                     bool& outExtractY);

  static std::vector<AnimEvent> load(const std::string& absPath);

  static void save(const std::string& absPath, const AnimationClip& clip);

};

}
