#pragma once

#include <glm/glm.hpp>

#include <optional>
#include <string_view>

namespace forge {

// RootMotionBegin payload: "scale;axes;maxVel" e.g. "0.5;xz", "2.0;xz;6.0"
// Every token is optional, empty tokens keep their default
struct RootMotionParams {
  float scale = 1.0f;
  glm::bvec3 axes = { true, false, true };
  std::optional<float> maxVelocity;
};

// Valid tokens are applied even when another token is malformed.
// Returns false if any present token is malformed or there are > 3 tokens
bool ParseRootMotionPayload(std::string_view payload, RootMotionParams& out);

// Whole (trimmed) token must be a finite number
bool ParseFloatStrict(std::string_view token, float& out);

}
