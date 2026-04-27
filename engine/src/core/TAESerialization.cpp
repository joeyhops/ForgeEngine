#include <forge/TAESerialization.h>
#include <forge/Logger.h>

#include <nlohmann/json.hpp>
#include <fstream>

namespace forge {

// Internal Helpers
static AnimEventType parseEventType(const std::string& s) {
  if (s == "SpawnHitbox") return AnimEventType::SpawnHitbox;
  if (s == "IFrame") return AnimEventType::IFrame;
  if (s == "SoundOneShot") return AnimEventType::SoundOneShot;
  if (s == "SFXSpawn") return AnimEventType::SFXSpawn;
  if (s == "ComboWindow") return AnimEventType::ComboWindow;
  if (s == "ParryWindow") return AnimEventType::ParryWindow;
  if (s == "DeflectWindow") return AnimEventType::DeflectWindow;
  if (s == "PerilousMark") return AnimEventType::PerilousMark;
  if (s == "RootMotionBegin") return AnimEventType::RootMotionBegin;
  if (s == "RootMotionEnd") return AnimEventType::RootMotionEnd;
  if (s == "RootMotionScale") return AnimEventType::RootMotionScale;
  if (s == "SetKinematic") return AnimEventType::SetKinematic;
  if (s == "RestorePhysics") return AnimEventType::RestorePhysics;
  if (s == "LockInput") return AnimEventType::LockInput;
  if (s == "UnlockInput") return AnimEventType::UnlockInput;
  if (s == "HyperArmorBegin") return AnimEventType::HyperArmorBegin;
  if (s == "HyperArmorEnd") return AnimEventType::HyperArmorEnd;
  if (s == "ScriptedSequence") return AnimEventType::ScriptedSequence;
  if (s == "StartTrail") return AnimEventType::StartTrail;
  if (s == "StopTrail") return AnimEventType::StopTrail;
  if (s == "SpawnImpactVFX") return AnimEventType::SpawnImpactVFX;
  LOG_WARN("[TAE] Unkown event type '{}' - defaulting to SpawnHitbox.", s);
  return AnimEventType::SpawnHitbox;
}

static std::string eventTypeToString(AnimEventType t) {
  switch (t) {
    case AnimEventType::SpawnHitbox: return "SpawnHitbox";
    case AnimEventType::IFrame: return "IFrame";
    case AnimEventType::SoundOneShot: return "SoundOneShot";
    case AnimEventType::SFXSpawn: return "SFXSpawn";
    case AnimEventType::ComboWindow: return "ComboWindow";
    case AnimEventType::ParryWindow: return "ParryWindow";
    case AnimEventType::DeflectWindow: return "DeflectWindow";
    case AnimEventType::PerilousMark: return "PerilousMark";
    case AnimEventType::RootMotionBegin: return "RootMotionBegin";
    case AnimEventType::RootMotionEnd: return "RootMotionEnd";
    case AnimEventType::RootMotionScale: return "RootMotionScale";
    case AnimEventType::SetKinematic: return "SetKinematic";
    case AnimEventType::RestorePhysics: return "RestorePhysics";
    case AnimEventType::LockInput: return "LockInput";
    case AnimEventType::UnlockInput: return "UnlockInput";
    case AnimEventType::HyperArmorBegin: return "HyperArmorBegin";
    case AnimEventType::HyperArmorEnd: return "HyperArmorEnd";
    case AnimEventType::ScriptedSequence: return "ScriptedSequence";
    case AnimEventType::StartTrail: return "StartTrail";
    case AnimEventType::StopTrail: return "StopTrail";
    case AnimEventType::SpawnImpactVFX: return "SpawnImpactVFX";
  }
  return "SpawnHitbox";
}

// Load (full)
std::vector<AnimEvent> TAESerialization::load(const std::string& absPath,
                                              std::string& outRootBone,
                                              bool& outExtractY)
{
  std::vector<AnimEvent> result;

  std::ifstream f(absPath);
  if (!f.is_open()) {
    LOG_WARN("[TAE] Cannot open sidecar: '{}'", absPath);
    return result;
  }

  nlohmann::json j;
  try {
    f >> j;
  } catch (const nlohmann::json::exception& e) {
    LOG_ERROR("[TAE] JSON Parse error in '{}': {}", absPath, e.what());
    return result;
  }

  // Top-level root motion fields
  outRootBone = j.value("rootBone", std::string("root"));
  outExtractY = j.value("extractRootY", false);

  if (!j.contains("events") || !j["events"].is_array()) {
    LOG_WARN("[TAE] Sidecar '{}' has no events array", absPath);
    return result;
  }

  for (const auto& ev : j["events"]) {
    AnimEvent e;
    e.startTime = ev.value("startTime", 0.0f);
    e.endTime = ev.value("endTime", 0.0f);
    e.payload = ev.value("payload", std::string{});
    e.type = parseEventType(ev.value("type", std::string{}));

    if (ev.contains("hitboxExtents") && ev["hitboxExtents"].is_array() && ev["hitboxExtents"].size() == 3) {
      e.hitboxExtents = glm::vec3(
        ev["hitboxExtents"][0].get<float>(),
        ev["hitboxExtents"][1].get<float>(),
        ev["hitboxExtents"][2].get<float>()
      );
    }
    result.push_back(e);
  }

  LOG_INFO("[TAE] Loaded {} events from '{}'", result.size(), absPath);
  return result;
}

// load (convenience)
std::vector<AnimEvent> TAESerialization::load(const std::string& absPath) {
  std::string rootBone;
  bool extractY = false;
  return load(absPath, rootBone, extractY);
}

void TAESerialization::save(const std::string& absPath, const AnimationClip& clip) {
  nlohmann::json j;
  j["clip"] = clip.name;
  j["rootBone"] = clip.rootBoneName;
  j["extractRootY"] = clip.extractRootY;

  nlohmann::json eventsArr = nlohmann::json::array();
  for (const auto& e : clip.events) {
    nlohmann::json ev;
    ev["type"] = eventTypeToString(e.type);
    ev["startTime"] = e.startTime;
    ev["endTime"] = e.endTime;
    if (!e.payload.empty())
      ev["payload"] = e.payload;
    if (e.type == AnimEventType::SpawnHitbox) {
      ev["hitboxExtents"] = { e.hitboxExtents.x, e.hitboxExtents.y, e.hitboxExtents.z };
    }
    eventsArr.push_back(ev);
  }
  j["events"] = eventsArr;

  std::ofstream f(absPath);
  if (!f.is_open()) {
    LOG_ERROR("[TAE] Could not write to sidecar: '{}'", absPath);
  }
  f << j.dump(2);
  LOG_INFO("[TAE] Saved {} events to '{}'", clip.events.size(), absPath);
}
}
