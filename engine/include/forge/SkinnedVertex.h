#pragma once

namespace forge {

constexpr int MAX_BONE_INFLUENCE = 4;

struct SkinnedVertex {
  float position[3] = { 0,0,0 };
  float normal[3] = { 0,1,0 };
  float texCoord[2] = { 0,0 };
  int boneIds[MAX_BONE_INFLUENCE] = { -1,-1,-1,-1 };
  float boneWeights[MAX_BONE_INFLUENCE] = { 0,0,0,0 };
  float tangent[4] = { 1, 0, 0, 1 };
};

}
