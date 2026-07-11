#include <forge/TaskSystem.h>

#include <forge/AnimationClip.h>
#include <forge/Bone.h>
#include <forge/Logger.h>

#include <algorithm>

namespace forge {

// PosePool

void PosePool::Init(int boneCount, int poolSize) {
  m_boneCount = boneCount;
  m_pool.clear();
  m_free.clear();
  m_pool.resize(poolSize);
  for (auto& buff : m_pool) {
    buff.localTransforms.assign(boneCount, glm::mat4(1.0f)); // presize each buffer
    buff.refCount = 0;
  }
  m_free.reserve(poolSize);
  for (auto& buff : m_pool) m_free.push_back(&buff);
}

PoseBuffer* PosePool::Acquire() {
  if (m_free.empty()) {
    // Pool exhausted: a graph-depth bug, not a runtime condition. Log loudly
    // and fall back to the last buffer rather than crashing
    LOG_ERROR("[PosePool] exhausted (capacity {}). Graph DAG deeper than pool "
              "size; increase poolSize.", (int)m_pool.size());
    return &m_pool.back();
  }
  PoseBuffer* buff = m_free.back();
  m_free.pop_back();
  buff->refCount = 1;
  return buff;
}

void PosePool::Release(PoseBuffer* buff) {
  if (!buff) return;
  if (--buff->refCount <= 0) {
    buff->refCount = 0;
    m_free.push_back(buff);
  }
}

// SampleTask -- decompress one clip frame at a fixed time into a pooled buffer
PoseBuffer* SampleTask::Execute(TaskSystem& /*sys*/, PosePool& pool) {
  PoseBuffer* buff = pool.Acquire();
  if (m_clip && m_skeleton && !m_skeleton->empty()) {
    m_clip->sample(m_time, *m_skeleton, buff->localTransforms);
  }
  return buff;
}

// BlendTask -- out - from*(1-w) + to*w 
PoseBuffer* BlendTask::Execute(TaskSystem& sys, PosePool& pool) {
  PoseBuffer* from = sys.Execute(m_a); // Weight applied to 'to'
  PoseBuffer* to = sys.Execute(m_b);
  if (!from) {
    LOG_WARN("[TaskSystem] BlendTask::Execute PoseBuffer 'from' not found, defaulting to 'to' by default.");
    return to;
  }

  if (!to) {
    LOG_WARN("[TaskSystem] BlendTask::Execute PoseBuffer 'to' not found, defaulting to 'from' by default.");
    return from;
  }

  const size_t count = std::min(from->localTransforms.size(),
                                to->localTransforms.size());
  for (size_t i = 0; i < count; ++i) {
    from->localTransforms[i] =
       from->localTransforms[i] * (1.0f - m_weight) +
       to->localTransforms[i] * m_weight;
  }

  pool.Release(to); // 'from' is reused as the result buffer
  return from;
}

// AdditiveTask -- out - base + additive*weight. Unregistered by any node in current phase 22; exists so a future
// additive-layer node has its primitive ready
PoseBuffer* AdditiveTask::Execute(TaskSystem& sys, PosePool& pool) {
  PoseBuffer* base = sys.Execute(m_base);
  PoseBuffer* add = sys.Execute(m_additive);
  if (!base) {
    LOG_WARN("[TaskSystem] AdditiveTask::Execute PoseBuffer 'base' not found, defaulting to 'add' by default.");
    return add;
  }

  if (!add) {
    LOG_WARN("[TaskSystem] AdditiveTask::Execute PoseBuffer 'add' not found, defaulting to 'base' by default.");
    return base;
  }

  const size_t count = std::min(base->localTransforms.size(),
                                add->localTransforms.size());
  for (size_t i = 0; i < count; ++i) {
    base->localTransforms[i] =
       base->localTransforms[i] + add->localTransforms[i] * m_weight;
  }

  pool.Release(add);
  return base;
}

// TaskSystem

void TaskSystem::Init(int boneCount, int poolSize) {
  m_pool.Init(boneCount, poolSize);
  m_tasks.clear();
  m_results.clear();
  m_executedCount = 0;
}

void TaskSystem::Reset() {
  m_tasks.clear(); // frees the frames task objects
  m_results.clear(); // drops cached buffer pointers
  m_executedCount = 0;
  // Pool buffers not freed here; they return to the free-list via Release()
  // during execution. Any buffer still 'in use' at this point is a leak
}

PoseBuffer* TaskSystem::Execute(TaskIndex idx) {
  if (idx < 0 || idx >= (TaskIndex)m_tasks.size()) return nullptr;

  if (m_results[idx]) {
    // Already executed (shared subtree). hand back the same buffer and bump its
    // ref count for this additional consumer
    ++m_results[idx]->refCount;
    return m_results[idx];
  }

  PoseBuffer* result = m_tasks[idx]->Execute(*this, m_pool);
  ++m_executedCount;
  m_results[idx] = result;
  return result;
}

PoseBuffer* TaskSystem::ExecutePrePhysicsTasks(TaskIndex root) {
  if (root < 0 || root >= (TaskIndex)m_tasks.size()) return nullptr;
  if (m_tasks[root]->phase != TaskPhase::PrePhysics) return nullptr;
  return Execute(root);
}

PoseBuffer* TaskSystem::ExecutePostPhysicsTasks(TaskIndex root) {
  if (root < 0 || root >= (TaskIndex)m_tasks.size()) return nullptr;
  // A pre-physics root was already executed and cached; a post-physics root
  // would layer corrections atop it.
  return GetResult(root) ? GetResult(root) : Execute(root);
}

PoseBuffer* TaskSystem::GetResult(TaskIndex idx) {
  if (idx < 0 || idx >= (TaskIndex)m_tasks.size()) return nullptr;
  return m_results[idx];
}

}
