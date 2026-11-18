#pragma once

#include "support/AnimTestUtils.h"

#include <forge/AnimGraph.h>
#include <forge/SampledEventsBuffer.h>
#include <forge/TaskSystem.h>

#include <vector>

namespace forge::test {

class NodeTicker {
public:
  explicit NodeTicker(std::vector<Bone> skeleton = makeSkeleton({ "root" }));

  AnimParamTable params;

  UpdateResult tick(AnimGraphNode& node, float dt) { return run(node, dt, nullptr); }
  UpdateResult tick(AnimGraphNode& node, float dt, SyncTrackTimeRange range) {
    return run(node, dt, &range);
  }

  const SampledEventsBuffer& events() const { return m_events; }
  const std::vector<glm::mat4>& lastPose() const { return m_lastPose; }
  int poseBuffersInUse() { return m_tasks.Pool().InUse(); }
  int tasksRegistered() const { return m_tasks.TaskCount(); }
  int tasksExecuted() const { return m_tasks.ExecutedCount(); }

private:
  UpdateResult run(AnimGraphNode& node, float dt, const SyncTrackTimeRange* range);

  std::vector<Bone> m_skeleton;
  TaskSystem m_tasks;
  SampledEventsBuffer m_events;
  std::vector<glm::mat4> m_lastPose;
};

}
