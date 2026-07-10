#pragma once

#ifdef FORGE_DEBUG

#include <glm/vec3.hpp>

#include <deque>
#include <string>
#include <vector>

namespace forge {

class AnimGraphNode;

class RootMotionDebugger {
public:
  struct Entry {
    const AnimGraphNode* node;
    std::string typeName;
    glm::vec3 localDelta;
    float weight;
  };

  void BeginFrame() { m_frame.clear(); }

  void Record(const AnimGraphNode* node, const char* typeName,
              const glm::vec3& localDelta, float weight) {
    m_frame.push_back({ node, typeName, localDelta, weight });
  }

  // Once per frame (game): feed the world-space trail
  // worldPos: chars world position this frame
  // worldRootDelta: the world-space translation root-motion applied this frame
  //                  (glm::vec3(0) when root motion did not drive mvmt)
  // rmActive: whether root motion drove movement this frame
  void PushSample(const glm::vec3& worldPos,
                  const glm::vec3& worldRootDelta,
                  bool rmActive);


  // world-basis maps local root-motion space to world (the chars facing transform)
  void Draw() const;
private:
  struct Sample {
    glm::vec3 worldPos;
    glm::vec3 worldRootDelta;
    bool rmActive;
    std::string dominant; // Dominant node type this frame, for coloring
  };

  std::string DominantType() const; // node in m_frame with the largest |localdelta|

  std::vector<Entry> m_frame; // per-node, cleared each BeginFrame
  std::deque<Sample> m_trail; // rolling world-space history (capped in PushSample)
};

}

#endif
