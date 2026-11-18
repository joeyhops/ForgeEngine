#include "support/ContentLint.h"
#include "support/TestPaths.h"

#include <forge/AnimGraph.h>
#include <forge/AssetManager.h>
#include <forge/RootMotionPayload.h>
#include <forge/TypeSystem.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>

namespace fs = std::filesystem;

namespace forge::test {
namespace {

std::optional<nlohmann::json> readJson(const fs::path& p) {
  std::ifstream f(p);
  if (!f) return std::nullopt;
  try {
    return nlohmann::json::parse(f);
  } catch (const nlohmann::json::exception&) {
    return std::nullopt;
  }
}

bool endsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// An "assets/..." path only resolves when the asset root is the relative
// string "assets/" (the game) and breaks with an absolute root (tests, tools),
// so it's flagged independently of whether the file exists
void checkContentPath(std::vector<std::string>& out, const std::string& where,
                      const std::string& rel) {
  if (rel.rfind("assets/", 0) == 0) out.push_back(where + ":path-has-assets-prefix");
  if (!assetExists(rel)) out.push_back(where + ":file-missing");
}

}

std::vector<std::string> findAssetFiles(const std::string& subdir, const std::string& suffix,
                                        bool recursive) {
  std::vector<std::string> out;
  const fs::path root = assetDir() / subdir;
  if (!fs::exists(root)) return out;

  auto isHidden = [](const fs::path& p) {
    const std::string name = p.filename().string();
    return !name.empty() && name[0] == '.';
  };
  auto consider = [&](const fs::directory_entry& e) {
    if (e.is_regular_file() && endsWith(e.path().filename().string(), suffix))
      out.push_back(assetRelative(e.path()));
  };
  if (recursive) {
    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
      if (isHidden(it->path())) {
        if (it->is_directory()) it.disable_recursion_pending();
        continue;
      }
      consider(*it);
    }
  } else {
    for (const auto& e : fs::directory_iterator(root))
      if (!isHidden(e.path())) consider(e);
  }
  // Directory iteration order is filesystem dependent
  std::sort(out.begin(), out.end());
  return out;
}

std::string assetRelative(const fs::path& abs) {
  const std::string a = abs.generic_string();
  const std::string root = assetDir().generic_string();
  if (a.rfind(root, 0) == 0) return a.substr(root.size());
  return fs::relative(abs, assetDir()).generic_string();
}

bool assetExists(const std::string& rel) {
  return fs::exists(AssetManager::resolvePath(rel));
}

std::vector<std::string> lintGraph(const std::string& rel) {
  std::vector<std::string> out;
  auto def = GraphDefinition::LoadFromJson(rel);
  if (!def) {
    out.push_back(rel + ":load-failed");
    return out;
  }

  const int n = static_cast<int>(def->m_nodeDefs.size());
  if (n == 0) {
    out.push_back(rel + ":no-nodes");
    return out;
  }
  if (static_cast<int>(def->m_nodeTypeIDs.size()) != n) out.push_back(rel + ":type-table-misaligned");
  if (def->m_rootNodeIndex < 0 || def->m_rootNodeIndex >= n) {
    out.push_back(rel + ":root-out-of-range");
    return out;
  }

  auto nodeTag = [&](int i) { return rel + ":node" + std::to_string(i); };
  auto get = [&](int i) -> const IAnimNodeDefinition* {
    return (i >= 0 && i < n) ? def->m_nodeDefs[i].get() : nullptr;
  };
  auto asClip = [&](int i) { return dynamic_cast<const ClipNode::Definition*>(get(i)); };

  // Mirrors the runtime isFinished() overrides: blends never override it so
  // they never finish, state machines depend on their active state
  std::function<bool(int, int)> canFinish = [&](int i, int depth) -> bool {
    if (depth > n) return false;
    const auto* d = get(i);
    if (const auto* c = dynamic_cast<const ClipNode::Definition*>(d)) return !c->loop;
    if (const auto* s = dynamic_cast<const ClipSelectorNode::Definition*>(d)) {
      for (const auto& e : s->entries)
        if (canFinish(e.nodeIndex, depth + 1)) return true;
      return false;
    }
    if (dynamic_cast<const StateMachineNode::Definition*>(d)) return true;
    return false;
  };

  std::vector<int> parentCount(n, 0);
  std::vector<std::vector<int>> children(n);
  auto link = [&](int parent, int child, const std::string& where) {
    if (child < 0 || child >= n) {
      out.push_back(where + ":index-out-of-range");
      return false;
    }
    children[parent].push_back(child);
    ++parentCount[child];
    return true;
  };

  for (int i = 0; i < n; ++i) {
    const std::string at = nodeTag(i);
    const auto* d = get(i);
    if (!d) {
      out.push_back(at + ":unknown-node-type");
      continue;
    }

    if (const auto* c = dynamic_cast<const ClipNode::Definition*>(d)) {
      if (c->clipFile.empty()) {
        out.push_back(at + ":clip-file-empty");
      } else {
        checkContentPath(out, at + ":clipFile", c->clipFile);
        if (!c->taeKey.empty()) {
          const auto sidecar = (fs::path(c->clipFile).parent_path() / "tae" / (c->taeKey + ".tae.json"))
                                 .generic_string();
          if (!assetExists(sidecar)) out.push_back(at + ":tae-sidecar-missing");
        }
      }
    } else if (const auto* b = dynamic_cast<const Blend1DNode::Definition*>(d)) {
      if (b->paramName.empty()) out.push_back(at + ":param-empty");
      if (b->childNodeIndices.empty()) out.push_back(at + ":no-children");
      if (b->thresholds.size() != b->childNodeIndices.size())
        out.push_back(at + ":threshold-count-mismatch");
      for (size_t k = 1; k < b->thresholds.size(); ++k) {
        if (!(b->thresholds[k] > b->thresholds[k - 1])) {
          out.push_back(at + ":thresholds-not-ascending");
          break;
        }
      }
      for (size_t k = 0; k < b->childNodeIndices.size(); ++k)
        link(i, b->childNodeIndices[k], at + ":child" + std::to_string(k));
    } else if (const auto* b2 = dynamic_cast<const Blend2DNode::Definition*>(d)) {
      if (b2->xParamName.empty() || b2->zParamName.empty()) out.push_back(at + ":param-empty");
      if (b2->clips.size() < 3) out.push_back(at + ":fewer-than-3-clips");
      for (size_t k = 0; k < b2->clips.size(); ++k) {
        const auto& ce = b2->clips[k];
        const std::string where = at + ":clip" + std::to_string(k);
        // Create() static_casts Blend2D children to ClipNode*, anything else is UB
        if (link(i, ce.nodeIndex, where) && !asClip(ce.nodeIndex))
          out.push_back(where + ":not-a-clipnode");
        for (size_t m = 0; m < k; ++m)
          if (b2->clips[m].x == ce.x && b2->clips[m].z == ce.z)
            out.push_back(where + ":duplicate-position");
      }
    } else if (const auto* s = dynamic_cast<const ClipSelectorNode::Definition*>(d)) {
      if (s->paramName.empty()) out.push_back(at + ":param-empty");
      if (s->entries.empty()) out.push_back(at + ":no-entries");
      std::set<int> seen;
      for (size_t k = 0; k < s->entries.size(); ++k) {
        const auto& e = s->entries[k];
        const std::string where = at + ":entry" + std::to_string(k);
        if (!seen.insert(e.selectorIndex).second) out.push_back(where + ":duplicate-selector-index");
        if (link(i, e.nodeIndex, where) && !asClip(e.nodeIndex))
          out.push_back(where + ":not-a-clipnode");
      }
    } else if (const auto* sm = dynamic_cast<const StateMachineNode::Definition*>(d)) {
      std::map<std::string, int> stateNode;
      if (sm->states.empty()) out.push_back(at + ":no-states");
      for (size_t k = 0; k < sm->states.size(); ++k) {
        const auto& st = sm->states[k];
        if (st.name.empty()) out.push_back(at + ":state" + std::to_string(k) + ":unnamed");
        if (!stateNode.emplace(st.name, st.nodeIndex).second)
          out.push_back(at + ":state-" + st.name + ":duplicate-name");
        link(i, st.nodeIndex, at + ":state-" + st.name);
      }
      if (!stateNode.count(sm->initialState)) out.push_back(at + ":initial-state-unknown");

      for (size_t k = 0; k < sm->transitions.size(); ++k) {
        const auto& t = sm->transitions[k];
        const std::string where = at + ":transition" + std::to_string(k);
        if (!t.fromState.empty() && !stateNode.count(t.fromState)) out.push_back(where + ":from-unknown");
        if (!stateNode.count(t.toState)) out.push_back(where + ":to-unknown");
        if (t.conditions.empty()) out.push_back(where + ":no-conditions");
        for (const auto& c : t.conditions) {
          if (c.type == ConditionType::SourceNodeFinished) {
            // findStateIndex("") is -1 at runtime, so this can never be true
            if (t.fromState.empty()) {
              out.push_back(where + ":source-finished-on-any-state");
            } else if (auto it = stateNode.find(t.fromState);
                       it != stateNode.end() && !canFinish(it->second, 0)) {
              out.push_back(where + ":source-can-never-finish");
            }
          } else if (c.param.empty()) {
            out.push_back(where + ":condition-param-empty");
          }
        }
      }
    }
  }

  // A shared child is advanced once per parent per frame
  for (int i = 0; i < n; ++i)
    if (parentCount[i] > 1)
      out.push_back(nodeTag(i) + ":shared-by-" + std::to_string(parentCount[i]) + "-parents");

  std::vector<bool> reached(n, false);
  std::vector<int> stack{ def->m_rootNodeIndex };
  while (!stack.empty()) {
    const int i = stack.back();
    stack.pop_back();
    if (reached[i]) continue;
    reached[i] = true;
    for (int c : children[i]) stack.push_back(c);
  }
  for (int i = 0; i < n; ++i)
    if (!reached[i]) out.push_back(nodeTag(i) + ":unreachable-from-root");

  return out;
}

// Reads raw JSON rather than going through TAESerialization, which is lossy by
// design (unknown types silently become SpawnHitbox)
std::vector<std::string> lintTae(const std::string& rel) {
  std::vector<std::string> out;
  const auto j = readJson(assetDir() / rel);
  if (!j) {
    out.push_back(rel + ":unparseable");
    return out;
  }
  if (!j->is_object() || !j->contains("events") || !(*j)["events"].is_array()) {
    out.push_back(rel + ":no-events-array");
    return out;
  }

  const EnumInfo* types = EnumRegistry::Get().GetEnumInfoByName("forge::AnimEventType");
  if (!types) {
    out.push_back(rel + ":enum-registry-missing");
    return out;
  }

  const auto& events = (*j)["events"];
  for (size_t i = 0; i < events.size(); ++i) {
    const std::string at = rel + ":event" + std::to_string(i);
    try {
      const auto& ev = events[i];
      if (!ev.is_object()) {
        out.push_back(at + ":not-an-object");
        continue;
      }

      const std::string typeName = ev.value("type", std::string{});
      int64_t typeValue = -1;
      if (typeName.empty()) out.push_back(at + ":type-missing");
      else if (!EnumRegistry::Get().TryGetValue(types->typeID, typeName.c_str(), typeValue))
        out.push_back(at + ":unknown-type-" + typeName);

      if (!ev.contains("startTime") || !ev.contains("endTime")) {
        out.push_back(at + ":time-missing");
      } else {
        const float start = ev.value("startTime", 0.0f);
        const float end = ev.value("endTime", 0.0f);
        if (start < 0.0f) out.push_back(at + ":negative-start");
        if (end < start) out.push_back(at + ":end-before-start");
      }
      if (ev.contains("hitboxExtents")) out.push_back(at + ":legacy-hitboxExtents");

      if (typeValue < 0) continue;
      const std::string payload = ev.value("payload", std::string{});
      switch (static_cast<AnimEventType>(typeValue)) {
        case AnimEventType::SpawnHitbox: {
          if (payload.empty()) out.push_back(at + ":hitbox-no-bone");
          if (!ev.contains("capsules") || !ev["capsules"].is_array() || ev["capsules"].empty()) {
            out.push_back(at + ":hitbox-no-capsules");
          } else {
            const auto& caps = ev["capsules"];
            for (size_t k = 0; k < caps.size(); ++k) {
              const auto& c = caps[k];
              const bool ok = c.is_object() &&
                              c.contains("p0") && c["p0"].is_array() && c["p0"].size() == 3 &&
                              c.contains("p1") && c["p1"].is_array() && c["p1"].size() == 3 &&
                              c.value("r", 0.0f) > 0.0f;
              if (!ok) out.push_back(at + ":capsule" + std::to_string(k) + "-malformed");
            }
          }
          break;
        }
        case AnimEventType::RootMotionBegin: {
          RootMotionParams p;
          if (!ParseRootMotionPayload(payload, p)) out.push_back(at + ":root-motion-payload-malformed");
          break;
        }
        case AnimEventType::RootMotionScale: {
          float s = 0.0f;
          if (!ParseFloatStrict(payload, s)) out.push_back(at + ":root-motion-scale-malformed");
          break;
        }
        case AnimEventType::ComboWindow:
          if (payload.empty()) out.push_back(at + ":combo-without-target");
          break;
        default:
          break;
      }
    } catch (const nlohmann::json::exception&) {
      // e.g. "startTime": "0.3" (string, not a number) makes value() throw
      out.push_back(at + ":malformed");
    }
  }

  if (j->contains("syncEvents")) {
    const auto& se = (*j)["syncEvents"];
    if (!se.is_array()) {
      out.push_back(rel + ":syncEvents-not-an-array");
    } else {
      if (se.size() == 1) out.push_back(rel + ":single-sync-event");
      for (size_t k = 0; k < se.size(); ++k) {
        const std::string at = rel + ":sync" + std::to_string(k);
        try {
          const float pct = se[k].value("percentage", -1.0f);
          if (pct < 0.0f || pct >= 1.0f) out.push_back(at + ":percentage-out-of-range");
          if (se[k].value("name", std::string{}).empty()) out.push_back(at + ":unnamed");
        } catch (const nlohmann::json::exception&) {
          out.push_back(at + ":malformed");
        }
      }
    }
  }
  return out;
}

std::vector<std::string> lintDefs() {
  std::vector<std::string> out;
  std::set<std::string> movesetIds;
  std::set<std::string> referencedFiles;
  std::vector<std::pair<std::string, std::string>> movesetRefs;

  using Visit = std::function<void(const nlohmann::json&, const std::string& where)>;
  auto forEachEntry = [&](const std::string& manifest, const char* arrayKey, const Visit& visit) {
    const auto j = readJson(assetDir() / manifest);
    if (!j) {
      out.push_back(manifest + ":unparseable");
      return;
    }
    if (!j->contains(arrayKey) || !(*j)[arrayKey].is_array()) {
      out.push_back(manifest + ":no-" + arrayKey + "-array");
      return;
    }
    const auto& arr = (*j)[arrayKey];
    for (size_t i = 0; i < arr.size(); ++i) {
      const std::string where = manifest + ":entry" + std::to_string(i);
      try {
        if (arr[i].contains("file")) {
          const std::string fileRel = arr[i]["file"].get<std::string>();
          referencedFiles.insert(fileRel);
          const auto ext = readJson(assetDir() / fileRel);
          if (!ext) {
            out.push_back(where + ":file-unreadable");
            continue;
          }
          visit(*ext, fileRel);
        } else {
          visit(arr[i], where);
        }
      } catch (const nlohmann::json::exception&) {
        out.push_back(where + ":malformed");
      }
    }
  };

  forEachEntry("data/movesets.json", "movesets", [&](const nlohmann::json& m, const std::string& where) {
    const std::string id = m.value("id", std::string{});
    if (id.empty()) {
      out.push_back(where + ":moveset-id-missing");
      return;
    }
    const std::string tag = "moveset:" + id;
    // AssetManager silently lets the later entry win
    if (!movesetIds.insert(id).second) out.push_back(tag + ":duplicate-id");
    if (m.contains("clips") && m["clips"].is_object()) {
      for (const auto& [key, path] : m["clips"].items()) {
        if (!path.is_string()) {
          out.push_back(tag + ":" + key + ":not-a-path");
          continue;
        }
        checkContentPath(out, tag + ":" + key, path.get<std::string>());
      }
    }
  });

  forEachEntry("data/weapons.json", "weapons", [&](const nlohmann::json& w, const std::string& where) {
    const std::string id = w.value("id", std::string{});
    if (id.empty()) {
      out.push_back(where + ":weapon-id-missing");
      return;
    }
    const std::string tag = "weapon:" + id;
    if (!w.contains("boneAttach")) out.push_back(tag + ":no-boneAttach");
    if (!w.contains("attacks") || !w["attacks"].is_object() || w["attacks"].empty())
      out.push_back(tag + ":no-attacks");
    else if (!w["attacks"].contains("r1"))
      out.push_back(tag + ":no-r1-attack");
    const std::string mesh = w.value("mesh", std::string{});
    if (!mesh.empty()) checkContentPath(out, tag + ":mesh", mesh);
    const std::string moveset = w.value("movesetId", std::string{});
    if (!moveset.empty()) movesetRefs.emplace_back(tag, moveset);
  });

  for (const auto& [tag, mid] : movesetRefs)
    if (!movesetIds.count(mid)) out.push_back(tag + ":unknown-moveset-" + mid);

  for (const char* dir : { "data/movesets", "data/weapons" })
    for (const auto& rel : findAssetFiles(dir, ".json", false))
      if (!referencedFiles.count(rel)) out.push_back(rel + ":not-referenced-by-manifest");

  return out;
}

}
