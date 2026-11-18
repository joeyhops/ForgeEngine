#include "support/AnimTestUtils.h"
#include "support/GraphBuilder.h"
#include "support/NodeTicker.h"

#include <forge/AnimGraph.h>

#include <catch2/catch_test_macros.hpp>

using namespace forge;
using namespace forge::test;

namespace {

struct SelectorRig {
  GraphBuilder builder;
  int16_t front, back, sel;
  std::shared_ptr<GraphDefinition> def;
  GraphInstance inst;
  NodeTicker ticker;

  SelectorRig()
    : front(builder.clip("roll_f", false))
    , back(builder.clip("roll_b", false))
    , sel(builder.selector("dodgeDir", { { 0, front }, { 1, back } }))
    , def(builder.root(sel).build())
    , inst(GraphInstance::Create(def)) {
    nodeAt<ClipNode>(inst, front).setClip(ClipBuilder("roll_f").duration(0.25f).build());
    nodeAt<ClipNode>(inst, back).setClip(ClipBuilder("roll_b").duration(0.25f).build());
  }

  ClipSelectorNode& node() { return nodeAt<ClipSelectorNode>(inst, sel); }
  ClipNode& clip(int16_t i) { return nodeAt<ClipNode>(inst, i); }
  UpdateResult tick(int dir, float dt = 0.125f) {
    ticker.params.setInt("dodgeDir", dir);
    return ticker.tick(node(), dt);
  }
};

}

TEST_CASE("ClipSelector advances only the entry selected by the int param",
          "[component][anim][selector]") {
  SelectorRig rig;
  rig.tick(0);
  rig.tick(0);
  CHECK(rig.clip(rig.front).m_time == 0.25f);
  CHECK(rig.clip(rig.back).m_time == 0.0f);
  CHECK(rig.node().isFinished());
}

TEST_CASE("ClipSelector restarts the incoming clip whenever the selection changes",
          "[component][anim][selector]") {
  SelectorRig rig;
  rig.tick(0);
  rig.tick(1);
  CHECK(rig.clip(rig.back).m_time == 0.125f);
  rig.tick(0);
  CHECK(rig.clip(rig.front).m_time == 0.125f);
}

// An out of range dodgeDir reports finished, so a SourceNodeFinished exit fires immediately
TEST_CASE("ClipSelector with an unknown index produces no pose and reports finished",
          "[component][anim][selector][characterization]") {
  SelectorRig rig;
  const UpdateResult r = rig.tick(7);
  CHECK(r.taskIdx == k_invalidTask);
  CHECK(rig.node().isFinished());
  CHECK(rig.node().getActiveClip() == nullptr);
}
