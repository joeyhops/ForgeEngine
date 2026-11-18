#include "support/NodeTicker.h"

namespace forge::test {

NodeTicker::NodeTicker(std::vector<Bone> skeleton) : m_skeleton(std::move(skeleton)) {
  m_tasks.Init(static_cast<int>(m_skeleton.size()), 16);
}

UpdateResult NodeTicker::run(AnimGraphNode& node, float dt, const SyncTrackTimeRange* range) {
  m_tasks.Reset();
  m_events.Clear();

  UpdateContext ctx{ dt, params, m_tasks, m_events, &m_skeleton 
#ifdef FORGE_DEBUG
    , nullptr
#endif
  };

  UpdateResult result = range ? node.update(ctx, *range) : node.update(ctx);
  
  m_lastPose.clear();
  if (PoseBuffer* pose = m_tasks.ExecutePrePhysicsTasks(result.taskIdx)) {
    m_lastPose = pose->localTransforms;
    m_tasks.Pool().Release(pose);
  }
  return result;
}

}
