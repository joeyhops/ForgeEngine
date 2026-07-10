#pragma once

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace forge {

// fwd declare; SampleTask holds these strictly as pointers.
class AnimationClip;
struct Bone;

using TaskIndex = int16_t;
static constexpr TaskIndex k_invalidTask = -1;

struct SampledEventRange {
  uint16_t begin = 0;
  uint16_t end = 0;
};

// Return value of every nodes Pass-1 update()
struct UpdateResult {
  TaskIndex taskIdx = k_invalidTask; // Handle into TaskSystem
  glm::vec3 rootMotion = glm::vec3(0.0f); // local-space delta this frame
  SampledEventRange eventRange = {};
};

struct PoseBuffer {
  std::vector<glm::mat4> localTransforms; // one local TRS per bone
  int refCount = 0;
};

enum class TaskPhase : uint8_t { PrePhysics, PostPhysics };

class PosePool {
public:
  PosePool() = default;
  explicit PosePool(int boneCount, int poolSize = 16) { Init(boneCount, poolSize); }

  void Init(int boneCount, int poolSize = 16);

  PoseBuffer* Acquire(); // refCount 0 -> 1
  void Release(PoseBuffer* buff); // refCount -> 0 returns to free-list

  int InUse() const { return (int)m_pool.size() - (int)m_free.size(); }
  int Capacity() const { return (int)m_pool.size(); }

private:
  std::vector<PoseBuffer> m_pool; // stable storage; never reallocated after first init
  std::vector<PoseBuffer*> m_free; // free-list (LIFO)
  int m_boneCount = 0;
};

class TaskSystem; // forward - ITask::Execute takes it by reference

class ITask {
public:
  virtual ~ITask() = default;

  virtual PoseBuffer* Execute(TaskSystem& system, PosePool& pool) = 0;

  TaskPhase phase = TaskPhase::PrePhysics;
};

class SampleTask : public ITask {
public:
  SampleTask(const AnimationClip* clip, float time, const std::vector<Bone>* skeleton)
      : m_clip(clip), m_time(time), m_skeleton(skeleton) {}
  PoseBuffer* Execute(TaskSystem& sys, PosePool& pool) override;
private:
  const AnimationClip* m_clip;
  float m_time;
  const std::vector<Bone>* m_skeleton;
};

class BlendTask : public ITask {
public:
  BlendTask(TaskIndex a, TaskIndex b, float weight) : m_a(a), m_b(b), m_weight(weight) {}
  PoseBuffer* Execute(TaskSystem& sys, PosePool& pool) override;
private:
  TaskIndex m_a, m_b;
  float m_weight;
};

class AdditiveTask : public ITask {
public:
  AdditiveTask(TaskIndex base, TaskIndex additive, float weight)
    : m_base(base), m_additive(additive), m_weight(weight) {}
  PoseBuffer* Execute(TaskSystem& sys, PosePool& pool) override;
private:
  TaskIndex m_base, m_additive;
  float m_weight;
};

class TaskSystem {
public:
  TaskSystem() = default;
  explicit TaskSystem(int boneCount, int poolSize = 16) { Init(boneCount, poolSize); }

  void Init(int boneCount, int poolSize = 16);

  //pass 1 - called by nodes during update() to enqueue deferred work.
  template <typename TTask, typename... Args>
  TaskIndex RegisterTask(TaskPhase phase, Args&&... args) {
    m_tasks.emplace_back(std::make_unique<TTask>(std::forward<Args>(args)...));
    m_tasks.back()->phase = phase;
    m_results.push_back(nullptr); // keep memo table in lockstep with m_tasks
    return static_cast<TaskIndex>(m_tasks.size() - 1);
  }

  // Pass 2 - memoised single-task execution (runs each task at most once)
  PoseBuffer* Execute(TaskIndex idx);

  // Pass 2 entry points, phase gated roots; mismatched phase is a no-op
  PoseBuffer* ExecutePrePhysicsTasks(TaskIndex root);
  PoseBuffer* ExecutePostPhysicsTasks(TaskIndex root);

  PoseBuffer* GetResult(TaskIndex idx); // cached buffer for an executed task or null
  void Reset(); // clear the DAG; once per frame before pass 1
  PosePool& Pool() { return m_pool; }

  int TaskCount() const { return (int)m_tasks.size(); }
  int ExecutedCount() const { return m_executedCount; } // zero-weight skip test
private:
  PosePool m_pool;
  std::vector<std::unique_ptr<ITask>> m_tasks;
  std::vector<PoseBuffer*> m_results; // parallel to m_tasks; null = not run
  int m_executedCount = 0;
};

}
