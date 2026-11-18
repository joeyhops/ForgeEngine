#include "support/LogCapture.h"

#include <forge/TaskSystem.h>

#include <glm/gtc/matrix_transform.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace forge;
using forge::test::LogCapture;
using Catch::Matchers::WithinAbs;

namespace {

class ConstPoseTask final : public ITask {
public:
  explicit ConstPoseTask(glm::mat4 m) : m_m(m) {}
  PoseBuffer* Execute(TaskSystem&, PosePool& pool) override {
    PoseBuffer* buff = pool.Acquire();
    for (auto& t : buff->localTransforms) t = m_m;
    return buff;
  }
private:
  glm::mat4 m_m;
};

glm::mat4 translation(float x, float y, float z) {
  return glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z));
}

}

TEST_CASE("PosePool tracks acquire and release",
          "[unit][anim][tasks]") {
  PosePool pool(4, 3);
  CHECK(pool.Capacity() == 3);
  CHECK(pool.InUse() == 0);

  PoseBuffer* a = pool.Acquire();
  REQUIRE(a != nullptr);
  CHECK(a->refCount == 1);
  CHECK(a->localTransforms.size() == 4);
  CHECK(pool.InUse() == 1);

  pool.Release(a);
  CHECK(pool.InUse() == 0);
  CHECK(a->refCount == 0);

  pool.Release(nullptr);
  CHECK(pool.InUse() == 0);
}

TEST_CASE("PosePool never hands out a buffer that is still in use when exhausted",
          "[unit][anim][tasks][!shouldfail][bug:A5]") {
  PosePool pool(1, 2);
  PoseBuffer* a = pool.Acquire();
  PoseBuffer* b = pool.Acquire();
  PoseBuffer* c = pool.Acquire(); 
  CHECK(c != a);
  CHECK(c != b);
}

TEST_CASE("PosePool double release does not let two owners share one buffer",
          "[unit][anim][tasks][!shouldfail][bug:A5]") {
  PosePool pool(1, 4);
  PoseBuffer* a = pool.Acquire();
  pool.Release(a);
  pool.Release(a);
  PoseBuffer* x = pool.Acquire();
  PoseBuffer* y = pool.Acquire();
  CHECK(x != y);
}

TEST_CASE("TaskSystem executes each task at most once and ref-counts shared results",
          "[unit][anim][tasks]") {
  TaskSystem tasks(2, 8);
  const TaskIndex t = tasks.RegisterTask<ConstPoseTask>(TaskPhase::PrePhysics, translation(1, 0, 0));

  PoseBuffer* p1 = tasks.Execute(t);
  PoseBuffer* p2 = tasks.Execute(t);
  REQUIRE(p1 != nullptr);
  CHECK(p1 == p2);
  CHECK(p1->refCount == 2);
  CHECK(tasks.ExecutedCount() == 1);

  tasks.Pool().Release(p1);
  tasks.Pool().Release(p2);
  CHECK(tasks.Pool().InUse() == 0);
}

TEST_CASE("TaskSystem never executes tasks the root does not reach",
          "[unit][anim][tasks]") {
  TaskSystem tasks(1, 8);
  const TaskIndex used = tasks.RegisterTask<ConstPoseTask>(TaskPhase::PrePhysics, glm::mat4(1.0f));
  const TaskIndex unused = tasks.RegisterTask<ConstPoseTask>(TaskPhase::PrePhysics, glm::mat4(1.0f));

  PoseBuffer* p = tasks.ExecutePrePhysicsTasks(used);
  REQUIRE(p != nullptr);
  CHECK(tasks.TaskCount() == 2);
  CHECK(tasks.ExecutedCount() == 1);
  CHECK(tasks.GetResult(unused) == nullptr);
  tasks.Pool().Release(p);
}

TEST_CASE("BlendTask lerps translations exactly and returns one live buffer",
          "[unit][anim][tasks]") {
  TaskSystem tasks(1, 8);
  const TaskIndex a = tasks.RegisterTask<ConstPoseTask>(TaskPhase::PrePhysics, translation(0, 0, 0));
  const TaskIndex b = tasks.RegisterTask<ConstPoseTask>(TaskPhase::PrePhysics, translation(2, 0, 0));
  const TaskIndex blend = tasks.RegisterTask<BlendTask>(TaskPhase::PrePhysics, a, b, 0.25f);

  PoseBuffer* out = tasks.ExecutePrePhysicsTasks(blend);
  REQUIRE(out != nullptr);
  CHECK_THAT(out->localTransforms[0][3].x, WithinAbs(0.5, 1e-6));
  CHECK(out->refCount == 1);
  CHECK(tasks.Pool().InUse() == 1);

  tasks.Pool().Release(out);
  CHECK(tasks.Pool().InUse() == 0);
}

TEST_CASE("BlendTask with a missing input passes the other input through",
          "[unit][anim][tasks]") {
  TaskSystem tasks(1, 8);
  const TaskIndex a = tasks.RegisterTask<ConstPoseTask>(TaskPhase::PrePhysics, translation(3, 0, 0));
  const TaskIndex blend = tasks.RegisterTask<BlendTask>(TaskPhase::PrePhysics, a, k_invalidTask, 0.5f);

  PoseBuffer* out = tasks.ExecutePrePhysicsTasks(blend);
  REQUIRE(out != nullptr);
  CHECK_THAT(out->localTransforms[0][3].x, WithinAbs(3.0, 1e-6));
  CHECK(LogCapture::contains(spdlog::level::warn, "'to' not found"));
  tasks.Pool().Release(out);
}

// BlendTask LERPS mat4s component-wise causing basis vectors to shrink to ~0.707
// leading to visible "limb shrink" mid-crossfade for rotations, as lerping
// component-wise for rotations is not a rotation. 
TEST_CASE("BlendTask blending two rigid transforms yields a rigid transform",
          "[unit][anim][tasks][!shouldfail][bug:A6]") {
  TaskSystem tasks(1, 8);
  const glm::mat4 r90 = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 1, 0));
  const TaskIndex a = tasks.RegisterTask<ConstPoseTask>(TaskPhase::PrePhysics, glm::mat4(1.0f));
  const TaskIndex b = tasks.RegisterTask<ConstPoseTask>(TaskPhase::PrePhysics, r90);
  const TaskIndex blend = tasks.RegisterTask<BlendTask>(TaskPhase::PrePhysics, a, b, 0.5f);

  PoseBuffer* out = tasks.ExecutePrePhysicsTasks(blend);
  REQUIRE(out != nullptr);
  const glm::mat4& m = out->localTransforms[0];
  CHECK_THAT(glm::length(glm::vec3(m[0])), WithinAbs(1.0, 1e-3));
  CHECK_THAT(glm::length(glm::vec3(m[2])), WithinAbs(1.0, 1e-3));
  tasks.Pool().Release(out);
}

TEST_CASE("TaskSystem pre-physics entry point ignores post-physics roots",
          "[unit][anim][tasks]") {
  TaskSystem tasks(1, 8);
  const TaskIndex post = tasks.RegisterTask<ConstPoseTask>(TaskPhase::PostPhysics, glm::mat4(1.0f));
  CHECK(tasks.ExecutePrePhysicsTasks(post) == nullptr);
  CHECK(tasks.ExecutedCount() == 0);
}

TEST_CASE("TaskSystem Reset clears the frame's DAG",
          "[unit][anim][tasks]") {
  TaskSystem tasks(1, 8);
  tasks.RegisterTask<ConstPoseTask>(TaskPhase::PrePhysics, glm::mat4(1.0f));
  tasks.Reset();
  CHECK(tasks.TaskCount() == 0);
  CHECK(tasks.ExecutedCount() == 0);
  CHECK(tasks.Execute(0) == nullptr);
}

