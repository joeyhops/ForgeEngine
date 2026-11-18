#include "support/Baseline.h"
#include "support/ContentLint.h"
#include "support/TestPaths.h"

#include <forge/AnimGraph.h>
#include <forge/AssetManager.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>
#include <tuple>

using namespace forge;
using namespace forge::test;

namespace {

struct ClipRef { std::string path, clipName, taeKey; };

std::vector<ClipRef> collectClipRefs() {
  std::set<std::tuple<std::string, std::string, std::string>> unique;

  for (const auto& graph : findAssetFiles("anim", ".json", false)) {
    auto def = GraphDefinition::LoadFromJson(graph);
    if (!def) continue;
    for (const auto& d : def->m_nodeDefs)
      if (const auto* c = dynamic_cast<const ClipNode::Definition*>(d.get()); c && !c->clipFile.empty())
        unique.emplace(c->clipFile, c->clipName, c->taeKey);
  }

  // Moveset swaps load with taeKey = action key (Animator::swapMovesetClips)
  AssetManager::loadMovesetDefs("data/movesets.json");
  for (const auto& [id, moveset] : AssetManager::getAllMovesetDefs())
    for (const auto& [actionKey, path] : moveset.clips)
      unique.emplace(path, std::string{}, actionKey);

  std::vector<ClipRef> out;
  for (const auto& [p, n, k] : unique) out.push_back({ p, n, k });
  return out;
}

}

TEST_CASE("Content: TAE events fit inside the clips they annotate (against baseline)",
          "[content][tae][lfs][slow]") {
  const auto refs = collectClipRefs();
  REQUIRE_FALSE(refs.empty());

  // A partial run would report the skipped clips' baseline entries as "fixed"
  int pointers = 0;
  for (const auto& r : refs)
    if (assetExists(r.path) && isLfsPointer(AssetManager::resolvePath(r.path))) ++pointers;
  if (pointers > 0)
    SKIP(pointers << " referenced clips are Git LFS pointers, a partial run would corrupt the baseline");

  constexpr float kSlack = 1.0f / 30.0f;
  std::vector<std::string> problems;
  for (const auto& r : refs) {
    if (!assetExists(r.path)) continue;
    const std::string tag = "clip:" + r.path + (r.taeKey.empty() ? "" : "|tae=" + r.taeKey);

    auto clip = AssetManager::loadAnimationClip(r.path, r.clipName, r.taeKey);
    if (!clip) {
      problems.push_back(tag + ":load-failed");
      continue;
    }
    if (!std::isfinite(clip->duration) || clip->duration <= 0.0f) {
      problems.push_back(tag + ":bad-duration");
      continue;
    }
    for (size_t i = 0; i < clip->events.size(); ++i) {
      const auto& e = clip->events[i];
      if (e.endTime > clip->duration + kSlack || e.startTime > clip->duration + kSlack)
        problems.push_back(tag + ":event" + std::to_string(i) + "-past-clip-end");
    }
    for (size_t i = 0; i < clip->syncTrack.events.size(); ++i)
      if (clip->syncTrack.events[i].percentage >= 1.0f)
        problems.push_back(tag + ":sync" + std::to_string(i) + "-past-clip-end");
  }
  AssetManager::clear();
  checkBaseline("clips.txt", problems);
}
