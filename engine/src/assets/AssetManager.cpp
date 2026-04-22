#include <assimp/material.h>
#include <forge/AssetManager.h>
#include <forge/Logger.h>
#include <forge/Shader.h>
#include <forge/Mesh.h>
#include <forge/SkinnedMesh.h>
#include <forge/SkinnedVertex.h>
#include <forge/Bone.h>
#include <forge/AnimationClip.h>

// Assimp
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <map>
#include <memory>
#include <string>
#include <functional>
#include <unordered_map>

//stb_image
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// JSON
#include <nlohmann/json.hpp>
#include <fstream>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <GL/glew.h>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace forge {

// Static members
std::string AssetManager::s_assetRoot;
std::unordered_map<std::string, ModelData> AssetManager::s_models;
std::unordered_map<std::string, SkinnedModelData> AssetManager::s_skinnedModels;
std::unordered_map<std::string, std::shared_ptr<AnimationClip>> AssetManager::s_clips;
std::unordered_map<std::string, std::shared_ptr<Shader>> AssetManager::s_shaders;
std::unordered_map<std::string, std::shared_ptr<Texture>> AssetManager::s_textures;
std::unordered_map<std::string, WeaponDef> AssetManager::s_weaponDefs;

// Helpers

// Helper: Convert Assimp row-major matrix to GLM column-major
static glm::mat4 aiMat4ToGlm(const aiMatrix4x4& m) {
  // aiMatrix4x4 is row-major; glm:mat4 constructor takes columns
  return glm::mat4(
    m.a1, m.b1, m.c1, m.d1,
    m.a2, m.b2, m.c2, m.d2,
    m.a3, m.b3, m.c3, m.d3,
    m.a4, m.b4, m.c4, m.d4
  );
}

// Helper:
static std::shared_ptr<Texture> loadMapTextureWithSuffix(const std::string& bareName, const std::string& suffix) {
  if (bareName == "__TB_empty") return nullptr;

  std::vector<std::string> exts = { ".png", ".jpg", ".tga" };
  for (const auto& ext : exts) {
    std::string path = AssetManager::resolvePath("textures/" + bareName + suffix + ext);
    if (fs::exists(path)) {
      return AssetManager::loadTextureAbsolute(path);
    }
  }

  return nullptr;
}

// Config
void AssetManager::setAssetRoot(const std::string& root) {
  s_assetRoot = root;
  // Ensure trailing separator
  if (!s_assetRoot.empty() && s_assetRoot.back() != '/')
    s_assetRoot += '/';
  LOG_INFO("[Assets] Root set to: {}", s_assetRoot);
}

const std::string& AssetManager::getAssetRoot() { return s_assetRoot; }

std::string AssetManager::resolvePath(const std::string& rel) {
  return s_assetRoot + rel;
}

std::shared_ptr<Shader> AssetManager::loadShader(
  const std::string& vertPath, const std::string& fragPath)
{
  std::string key = vertPath + "|" + fragPath;
  auto it = s_shaders.find(key);
  if (it != s_shaders.end()) {
    LOG_TRACE("[Assets] Shader cache hit: {}", key);
    return it->second;
  }

  auto shader = std::make_shared<Shader>(
    resolvePath(vertPath),
    resolvePath(fragPath)
  );
  s_shaders[key] = shader;
  return shader;
}

std::shared_ptr<Texture> AssetManager::loadTextureAbsolute(const std::string& absPath) {
  // Normalize path seps for consistent cache keys
  std::string key = fs::path(absPath).generic_string();
  
  auto it = s_textures.find(key);
  if (it != s_textures.end()) return it->second;

  stbi_set_flip_vertically_on_load_thread(true);
  
  int width, height, channels;
  unsigned char* data = stbi_load(absPath.c_str(), &width, &height, &channels, 0);
  if (!data) {
    LOG_WARN("[Assets] Could not load texture: {}", absPath);
    return nullptr;
  }

  GLenum format = (channels == 4) ? GL_RGBA :
                  (channels == 3) ? GL_RGB : GL_RED;

  auto tex = std::make_shared<Texture>();
  tex->width = width;
  tex->height = height;
  tex->path = absPath;

  glGenTextures(1, &tex->id);
  glBindTexture(GL_TEXTURE_2D, tex->id);

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  glTexImage2D(GL_TEXTURE_2D, 0, format, width, height,
               0, format, GL_UNSIGNED_BYTE, data);
  glGenerateMipmap(GL_TEXTURE_2D);

  // Filtering - trilinear for quality, wraps for tiling textures
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

  glBindTexture(GL_TEXTURE_2D, 0);
  stbi_image_free(data);

  LOG_INFO("[Assets] Loaded texture '{}' - {}x{} ({} ch)",
           absPath, width, height, channels);

  s_textures[absPath] = tex;
  return tex;
}

std::shared_ptr<Texture> AssetManager::loadTexture(const std::string& path) {
  return loadTextureAbsolute(resolvePath(path));
}

std::shared_ptr<Texture> AssetManager::loadMapTexture(const std::string& bareName) {
  if (bareName == "__TB_empty") return nullptr; // Special TrenchBroom editor texture

  auto tex = loadMapTextureWithSuffix(bareName, "");
  if (tex) return tex;

  tex = loadMapTextureWithSuffix(bareName, "_color");
  if (tex) return tex;

  // Fallback: gen a 2x2 magenta/black checkerboard
  std::string fallbackKey = "__fallback_magenta";
  if (s_textures.find(fallbackKey) != s_textures.end()) return s_textures[fallbackKey];

  tex = std::make_shared<Texture>();
  tex->width = 2; tex->height = 2; tex->path = fallbackKey;
  unsigned char data[16] = {
    255, 0, 255, 255,  0, 0, 0, 255,
    0, 0, 0, 255,      255, 0, 255, 255
  };
  glGenTextures(1, &tex->id);
  glBindTexture(GL_TEXTURE_2D, tex->id);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glBindTexture(GL_TEXTURE_2D, 0);

  LOG_WARN("[Assets] Map texture '{}' not found. Using fallback", bareName);
  s_textures[fallbackKey] = tex;
  return tex;
}

std::shared_ptr<Texture> AssetManager::loadMapNormalTexture(const std::string& bareName) {
  auto tex = loadMapTextureWithSuffix(bareName, "_normal");
  if (tex) return tex;
  return loadMapTextureWithSuffix(bareName, "_normalgl");
}

std::shared_ptr<Texture> AssetManager::loadMapRoughnessTexture(const std::string& bareName) {
  return loadMapTextureWithSuffix(bareName, "_roughness");
}

std::shared_ptr<Texture> AssetManager::loadMapMetallicTexture(const std::string& bareName) {
  return loadMapTextureWithSuffix(bareName, "_metallic");
}

std::shared_ptr<Texture> AssetManager::loadMapAOTexture(const std::string& bareName) {
  return loadMapTextureWithSuffix(bareName, "_ao");
}

ModelData AssetManager::loadModel(const std::string& path) {
  auto it = s_models.find(path);
  if (it != s_models.end()) return it->second;

  std::string absPath = resolvePath(path);

  Assimp::Importer importer;
  const unsigned int flags =
    aiProcess_Triangulate           |   // Quads → triangles
    aiProcess_GenSmoothNormals      |   // Generate normals if absent
    aiProcess_FlipUVs               |   // OpenGL UV origin = bottom-left
    aiProcess_JoinIdenticalVertices |   // Deduplicate verts
    aiProcess_PreTransformVertices  |   // Bake node transforms into mesh
    aiProcess_OptimizeMeshes;           // Merge small meshes where possible

  const aiScene* scene = importer.ReadFile(absPath, flags);
  if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
    throw std::runtime_error("[Assets] Assimp error loading '" + path
                             + "': " + importer.GetErrorString());
  }
  if (scene->mNumMeshes == 0)
    throw std::runtime_error("[Assets] No meshes in: " + path);

  fs::path modelDir = fs::path(absPath).parent_path();
  
  auto resolveTexture = [&](aiMaterial* mat) -> std::shared_ptr<Texture> {
    aiString texPath;
    if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) != AI_SUCCESS)
      return nullptr;

    std::vector<fs::path> candidates = {
      fs::path(texPath.C_Str()),
      modelDir / texPath.C_Str(),
      modelDir / fs::path(texPath.C_Str()).filename(),
      modelDir / "Textures" / fs::path(texPath.C_Str()).filename(),
    };

    for (const auto& candidate : candidates) {
      if (fs::exists(candidate)) {
        LOG_INFO("[Assets] Texture resolved: {}", candidate.generic_string());
        return loadTextureAbsolute(candidate.generic_string());
      }
    }

    if (std::string(texPath.C_Str()).find("__TB_empty") == std::string::npos)
      LOG_WARN("[Assets] Texture not found for '{}': {}", path, texPath.C_Str());

    return nullptr;
  };

  std::map<unsigned int, std::vector<Vertex>> matVerts;
  std::map<unsigned int, std::vector<unsigned int>> matIdx;

  // Merge all meshes in the file into one
  std::vector<glm::vec3> cpuPositions;
  std::vector<uint32_t> cpuIndices;

  unsigned int physicsOffset = 0;

  for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
    aiMesh* aiM = scene->mMeshes[m];
    unsigned int matIdx_ = aiM->mMaterialIndex;

    unsigned int localOffset = static_cast<unsigned int>(matVerts[matIdx_].size());

    for (unsigned int i = 0; i < aiM->mNumVertices; i++) {
      Vertex v;
      v.position[0] = aiM->mVertices[i].x;
      v.position[1] = aiM->mVertices[i].y;
      v.position[2] = aiM->mVertices[i].z;

      if (aiM->HasNormals()) {
          v.normal[0] = aiM->mNormals[i].x;
          v.normal[1] = aiM->mNormals[i].y;
          v.normal[2] = aiM->mNormals[i].z;
      }

      // First UV channel — where texture coordinates live
      if (aiM->HasTextureCoords(0)) {
          v.texCoord[0] = aiM->mTextureCoords[0][i].x;
          v.texCoord[1] = aiM->mTextureCoords[0][i].y;
      }

      matVerts[matIdx_].push_back(v);

      // Phys vertex (pos only, no normals/UVs)
      cpuPositions.push_back(glm::vec3(
        v.position[0],
        v.position[1],
        v.position[2]
      ));
    }

    for (unsigned int f = 0; f < aiM->mNumFaces; f++) {
      const aiFace& face = aiM->mFaces[f];
      for (unsigned int j = 0; j < face.mNumIndices; j++) {
        unsigned int localIdx = face.mIndices[j];
        matIdx[matIdx_].push_back(localIdx + localOffset);
        cpuIndices.push_back(static_cast<uint32_t>(localIdx + physicsOffset));
      }
    }
    physicsOffset += aiM->mNumVertices;
  }

  glm::vec3 bmin(1e9f), bmax(-1e9f);
  for (const auto& p : cpuPositions) {
    bmin = glm::min(bmin, p);
    bmax = glm::max(bmax, p);
  }
  glm::vec3 size = bmax - bmin;
  LOG_INFO("[Assets] '{}' bounds min:({:.2f},{:.2f},{:.2f}) max:({:.2f},{:.2f},{:.2f}) size:({:.2f},{:.2f},{:.2f})",
           path, bmin.x,bmin.y,bmin.z, bmax.x,bmax.y,bmax.z, size.x,size.y,size.z);
  LOG_INFO("[Assets] '{}' — {} verts, {} physics tris, {} materials",
           path, cpuPositions.size(), cpuIndices.size() / 3, matVerts.size());

  ModelData result;
  result.positions = std::move(cpuPositions);
  result.indices = std::move(cpuIndices);

  for (auto& [matIndex, verts] : matVerts) {
    ModelData::SubMesh sub;
    sub.mesh = std::make_shared<Mesh>(verts, matIdx[matIndex]);

    if (matIndex < scene->mNumMaterials)
      sub.texture = resolveTexture(scene->mMaterials[matIndex]);

    result.subMeshes.push_back(std::move(sub));
  }

  if (!result.subMeshes.empty()) {
    result.mesh = result.subMeshes[0].mesh;
    result.texture = result.subMeshes[0].texture;
  }

  s_models[path] = result;
  return result;
}

SkinnedModelData AssetManager::loadSkinnedModel(const std::string& path) {
  auto it = s_skinnedModels.find(path);
  if (it != s_skinnedModels.end()) return it->second;

  std::string absPath = resolvePath(path);

  Assimp::Importer importer;
  const unsigned int flags =
    aiProcess_Triangulate | // Quads -> triangles
    aiProcess_GenSmoothNormals | // gen normals if missing
    aiProcess_FlipUVs | // OpenGL UV origin = btm-left
    aiProcess_JoinIdenticalVertices | // de dupe
    aiProcess_LimitBoneWeights; // clamp to 4 influences per vertex

  const aiScene* scene = importer.ReadFile(absPath, flags);
  if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
    throw std::runtime_error("[Assets] loadSkinnedModel failed for '" + path + "': " + importer.GetErrorString());

  // step 1: collect bone names + offset matrices
  // Walk every mesh and register unique bones into boneNameToIndex
  std::unordered_map<std::string, int> boneNameToIndex;
  std::vector<glm::mat4> offsetMatrices;

  for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
    aiMesh* aiM = scene->mMeshes[m];
    for (unsigned int b = 0; b < aiM->mNumBones; b++) {
      aiBone* bone = aiM->mBones[b];
      std::string bname(bone->mName.C_Str());
      if (boneNameToIndex.find(bname) == boneNameToIndex.end()) {
        int idx = (int)boneNameToIndex.size();
        boneNameToIndex[bname] = idx;
        offsetMatrices.push_back(aiMat4ToGlm(bone->mOffsetMatrix));
      }
    }
  }

  int boneCount = (int)boneNameToIndex.size();
  LOG_INFO("[Assets] '{}' - {} bones", path, boneCount);

  // Step 2: Build skeleton with parent indices + local transforms
  // Walk the aiNode tree to find each bones parent and default-pose transform
  std::vector<Bone> skeleton(boneCount);
  for (auto& [bname, idx] : boneNameToIndex) {
    skeleton[idx].name = bname;
    skeleton[idx].offsetMatrix = offsetMatrices[idx];
    skeleton[idx].parentIndex = -1;
    skeleton[idx].localTransform = glm::mat4(1.0f);
  }

  std::function<void(aiNode*, int)> walkNodes;
  walkNodes = [&](aiNode* node, int parentBoneIdx) {
    std::string nodeName(node->mName.C_Str());
    int thisBoneIdx = -1;

    auto found = boneNameToIndex.find(nodeName);
    if (found != boneNameToIndex.end()) {
      thisBoneIdx = found->second;
      skeleton[thisBoneIdx].parentIndex = parentBoneIdx;
      skeleton[thisBoneIdx].localTransform = aiMat4ToGlm(node->mTransformation);
    }

    int nextParent = (thisBoneIdx >= 0) ? thisBoneIdx : parentBoneIdx;
    for (unsigned int i = 0; i < node->mNumChildren; i++)
      walkNodes(node->mChildren[i], nextParent);
  };
  walkNodes(scene->mRootNode, -1);

  for (auto& bone: skeleton)
    LOG_INFO(" bone: {}", bone.name);

  // Step 3: Build skinned vertex array
  std::vector<SkinnedVertex> allVerts;
  std::vector<unsigned int> allIdx;
  unsigned int indexOffset = 0;

  for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
    aiMesh* aiM = scene->mMeshes[m];

    // Accumulate bone influence per-vertex
    struct Influence { int boneIdx; float weight; };
    std::vector<std::vector<Influence>> inf(aiM->mNumVertices);

    for (unsigned int b = 0; b < aiM->mNumBones; b++) {
      aiBone* bone = aiM->mBones[b];
      int boneIdx = boneNameToIndex[bone->mName.C_Str()];
      for (unsigned int w = 0; w < bone->mNumWeights; w++) {
        unsigned int vi = bone->mWeights[w].mVertexId;
        inf[vi].push_back({ boneIdx, bone->mWeights[w].mWeight });
      }
    }

    for (unsigned int i = 0; i < aiM->mNumVertices; i++) {
      SkinnedVertex v;

      v.position[0] = aiM->mVertices[i].x;
      v.position[1] = aiM->mVertices[i].y;
      v.position[2] = aiM->mVertices[i].z;

      if (aiM->HasNormals()) {
        v.normal[0] = aiM->mNormals[i].x;
        v.normal[1] = aiM->mNormals[i].y;
        v.normal[2] = aiM->mNormals[i].z;
      }

      if (aiM->HasTextureCoords(0)) {
        v.texCoord[0] = aiM->mTextureCoords[0][i].x;
        v.texCoord[1] = aiM->mTextureCoords[0][i].y;
      }

      // Sort influences by weight descending, keep top 4
      auto& influences = inf[i];
      if (influences.size() > 4) {
        std::sort(influences.begin(), influences.end(),
                  [](const Influence& a, const Influence& b) { return a.weight > b.weight; });
        influences.resize(4);
      }

      // Normalize weights to sum to 1.0
      float total = 0.0f;
      for (auto& infl : influences) total += infl.weight;

      for (int j = 0; j < (int)influences.size() && j < 4; j++) {
        v.boneIds[j] = influences[j].boneIdx;
        v.boneWeights[j] = (total > 1e-6f) ? influences[j].weight / total : 0.0f;
      }

      allVerts.push_back(v);
    }
    
    for (unsigned int f = 0; f < aiM->mNumFaces; f++) {
      const aiFace& face = aiM->mFaces[f];
      for (unsigned int j = 0; j < face.mNumIndices; j++)
        allIdx.push_back(face.mIndices[j] + indexOffset);
    }
    indexOffset += aiM->mNumVertices;
  }

  // Step 4: resolve texture (same strat as loadModel)
  std::shared_ptr<Texture> texture = nullptr;
  fs::path modelDir = fs::path(absPath).parent_path();

  if (scene->mNumMeshes > 0 && scene->mNumMaterials > 0) {
    aiMaterial* mat = scene->mMaterials[scene->mMeshes[0]->mMaterialIndex];
    aiString texPath;
    if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
      std::vector<fs::path> candidates = {
        fs::path(texPath.C_Str()),
        modelDir / texPath.C_Str(),
        modelDir / fs::path(texPath.C_Str()).filename(),
        modelDir / "Textures" / fs::path(texPath.C_Str()).filename(),
      };
      for (auto& cand : candidates) {
        if (fs::exists(cand)) {
          texture = loadTextureAbsolute(cand.generic_string());
          break;
        }
      }
      if (!texture)
        LOG_WARN("[Assets] Texture not found for skinned model '{}': {}",
                 path, texPath.C_Str());
    }

    // GLTF embedded texture fallback
    if (!texture && scene->mNumTextures > 0) {
      const aiTexture* embTex = scene->mTextures[0];
      if (embTex->mHeight == 0) {
        // Compressed - load via stbi from memory
        stbi_set_flip_vertically_on_load_thread(true);
        int w, h, ch;
        unsigned char* data = stbi_load_from_memory(
          reinterpret_cast<const unsigned char*>(embTex->pcData),
          embTex->mWidth, &w, &h, &ch, 0);
        if (data) {
          GLenum fmt = (ch == 4) ? GL_RGBA : GL_RGB;
          auto tex = std::make_shared<Texture>();
          tex->width  = w;
          tex->height = h;
          tex->path   = absPath + "[embedded]";
          glGenTextures(1, &tex->id);
          glBindTexture(GL_TEXTURE_2D, tex->id);
          glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
          glGenerateMipmap(GL_TEXTURE_2D);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
          glBindTexture(GL_TEXTURE_2D, 0);
          stbi_image_free(data);
          texture = tex;
          LOG_INFO("[Assets] Loaded embedded GLTF texture from '{}'", path);
        }
      }
    }
  }

  LOG_INFO("[Assets] loadSkinnedModel '{}' - {} verts, {} idx, {} bones",
           path, allVerts.size(), allIdx.size(), boneCount);

  SkinnedModelData result;
  result.mesh = std::make_shared<SkinnedMesh>(allVerts, allIdx);
  result.skeleton = std::move(skeleton);
  result.texture = texture;

  loadSocketsFor(result, path);

  s_skinnedModels[path] = result;
  return result;
}

std::shared_ptr<AnimationClip> AssetManager::loadAnimationClip(
    const std::string& path,
    const std::string& clipName)
{
  // Cache key combines path + clip name so the same file can yield
  // multiple named clips without reloading.
  std::string cacheKey = path + "|" + clipName;
  auto it = s_clips.find(cacheKey);
  if (it != s_clips.end()) return it->second;
 
  std::string absPath = resolvePath(path);
 
  // For animation-only loading we skip mesh processing entirely.
  Assimp::Importer importer;
  const aiScene* scene = importer.ReadFile(absPath, 0);
  if (!scene || scene->mNumAnimations == 0) {
    LOG_WARN("[Assets] No animations in: {}", path);
    return nullptr;
  }
 
  // Find animation — by name if provided, otherwise take the first one.
  aiAnimation* anim = scene->mAnimations[0];
  if (!clipName.empty()) {
    for (unsigned int i = 0; i < scene->mNumAnimations; i++) {
      if (std::string(scene->mAnimations[i]->mName.C_Str()) == clipName) {
        anim = scene->mAnimations[i];
        break;
      }
    }
  }
 
  float ticksPerSec = (anim->mTicksPerSecond > 0.0)
    ? static_cast<float>(anim->mTicksPerSecond)
    : 60.0f;
 
  auto clip = std::make_shared<AnimationClip>();
  clip->name        = clipName.empty() ? std::string(anim->mName.C_Str()) : clipName;
  clip->ticksPerSec = ticksPerSec;
  clip->duration    = static_cast<float>(anim->mDuration / anim->mTicksPerSecond);
 
  clip->tracks.reserve(anim->mNumChannels);
 
  for (unsigned int c = 0; c < anim->mNumChannels; c++) {
    aiNodeAnim* channel = anim->mChannels[c];

    // Temp diagnostic code
    if (std::string(channel->mNodeName.C_Str()) == "mixamorig:LeftArm") {
      LOG_INFO("[DEBUG] LeftArm keys: {} pos {} rot {} scale",
               channel->mNumPositionKeys,
               channel->mNumRotationKeys,
               channel->mNumScalingKeys);

      if (channel->mNumRotationKeys > 1) {
        auto& r0 = channel->mRotationKeys[0];
        auto& r1 = channel->mRotationKeys[channel->mNumRotationKeys - 1];
        LOG_INFO("[DEBUG] LeftArm rot[0]: t={:.4f} q=({:.3f},{:.3f},{:.3f},{:.3f})",
                 r0.mTime, r0.mValue.w, r0.mValue.x, r0.mValue.y, r0.mValue.z);
        LOG_INFO("[DEBUG] LeftArm rot[last]: t={:.4f} q=({:.3f},{:.3f},{:.3f},{:.3f})",
                 r1.mTime, r1.mValue.w, r1.mValue.x, r1.mValue.y, r1.mValue.z);
      }
    }

    BoneTrack track;
    track.boneName = channel->mNodeName.C_Str();
 
    // Use the maximum keyframe count across the three arrays as the timeline.
    // For Mixamo / Blender GLB data all three counts are equal; for some FBX
    // files they may differ — the min() clamp in the index lookups handles that.
    size_t numKeys = std::max({
      (size_t)channel->mNumPositionKeys,
      (size_t)channel->mNumRotationKeys,
      (size_t)channel->mNumScalingKeys
    });
 
    track.keyframes.reserve(numKeys);
 
    for (size_t k = 0; k < numKeys; k++) {
      BoneKeyFrame frame;
 
      // Position
      unsigned int pi = static_cast<unsigned int>(
        std::min(k, (size_t)(channel->mNumPositionKeys - 1)));
      frame.position = glm::vec3(
        channel->mPositionKeys[pi].mValue.x,
        channel->mPositionKeys[pi].mValue.y,
        channel->mPositionKeys[pi].mValue.z
      ) ;

      double rawTime;
      if (k < channel->mNumPositionKeys) rawTime = channel->mPositionKeys[k].mTime;
      else if (k < channel->mNumRotationKeys) rawTime = channel->mRotationKeys[k].mTime;
      else rawTime = channel->mScalingKeys[k].mTime;
      frame.time = static_cast<float>(rawTime / anim->mTicksPerSecond);
      // Rotation
      unsigned int ri = static_cast<unsigned int>(
        std::min(k, (size_t)(channel->mNumRotationKeys - 1)));
      auto& r = channel->mRotationKeys[ri].mValue;
      frame.rotation = glm::quat(r.w, r.x, r.y, r.z);
 
      // Scale
      unsigned int si = static_cast<unsigned int>(
        std::min(k, (size_t)(channel->mNumScalingKeys - 1)));
      auto& s = channel->mScalingKeys[si].mValue;
      frame.scale = glm::vec3(s.x, s.y, s.z);
 
      track.keyframes.push_back(frame);
    }
 
    clip->trackIndex[track.boneName] = (int)clip->tracks.size();
    clip->tracks.push_back(std::move(track));
  }
 
  LOG_INFO("[Assets] Animation '{}' — {:.2f}s  {} tracks  ({} tps)",
           clip->name, clip->duration, clip->tracks.size(), (int)ticksPerSec);
  for (auto& track : clip->tracks)
    LOG_INFO(" track: {}", track.boneName);
 
  s_clips[cacheKey] = clip;
  return clip;
}

void AssetManager::loadWeponDefs(const std::string& relativePath) {
  std::string fullPath = resolvePath(relativePath);
  std::ifstream file(fullPath);
  if (!file.is_open()) {
    LOG_ERROR("[AssetManager] loadWeaponDefs: cannot open '{}'", fullPath);
    return;
  }

  nlohmann::json j;
  try {
    file >> j;
  } catch (const nlohmann::json::exception& e) {
    LOG_ERROR("[AssetManager] loadWeaponDefs: JSON parse error: {}", e.what());
    return;
  }

  for (const auto& w : j.at("weapons")) {
    WeaponDef def;
    def.id = w.at("id").get<std::string>();
    def.meshPath = w.value("mesh", "");

    if (w.contains("attachSocket"))
      def.attachSocket = w.at("attachSocket").get<std::string>();
    else if (w.contains("attachBone"))
      def.attachSocket = w.at("attachBone").get<std::string>();
    else
      throw std::runtime_error(
        "WeaponDef '" + def.id + "': missing attachSocket (or legacy attachBone)");

    if (w.contains("hitSocket"))
      def.hitSocket = w.at("hitSocket").get<std::string>();
    else if (w.contains("hitboxBone"))
      def.hitSocket = w.at("hitboxBone").get<std::string>();
    else
      def.hitSocket = def.attachSocket; // default: hit origin = attack

    if (w.contains("hitboxHalfExtents")) {
      auto& h = w.at("hitboxHalfExtents");
      def.hitboxHalfExtents = { h[0], h[1], h[2] };
    }
    if (w.contains("hitboxOffset")) {
      auto& o = w.at("hitboxOffset");
      def.hitboxOffset = { o[0], o[1], o[2] };
    }

    for (const auto& [atkName, atkData] : w.at("attacks").items()) {
      AttackData atk;
      atk.name = atkName;
      atk.damage = atkData.value("damage", atk.damage);
      atk.poiseDamage = atkData.value("poiseDamage", atk.poiseDamage);
      atk.staminaCost = atkData.value("staminaCost", atk.staminaCost);
      atk.startupTime = atkData.value("startupTime", atk.startupTime);
      atk.activeTime = atkData.value("activeTime", atk.activeTime);
      atk.recoveryTime = atkData.value("recoveryTime", atk.recoveryTime);
      atk.range = atkData.value("range", atk.range);
      atk.angle = atkData.value("angle", atk.angle);
      atk.damageType = atkData.value("damageType", atk.damageType);
      def.attacks[atkName] = atk;
    }

    s_weaponDefs[def.id] = std::move(def);
    LOG_INFO("[AssetManager] Loaded weapon def '{}'", s_weaponDefs.begin()->first);
  }

  LOG_INFO("[AssetManager] loadedWeaponDefs: {} weapons loaded from '{}'", s_weaponDefs.size(), relativePath);
}

const WeaponDef* AssetManager::getWeaponDef(const std::string& weaponId) {
  auto it = s_weaponDefs.find(weaponId);
  return (it != s_weaponDefs.end()) ? &it->second : nullptr;
}

void AssetManager::clear() {
  s_skinnedModels.clear();
  s_clips.clear();
  s_models.clear();
  s_textures.clear();
  s_shaders.clear();
  s_weaponDefs.clear();
  LOG_INFO("[Assets] Cache cleared");
}

void AssetManager::printStats() {
  LOG_INFO("[Assets] Cache: {} models, {} textures, {} Shaders cached",
           s_models.size(), s_textures.size(), s_shaders.size());
}

void AssetManager::loadSocketsFor(SkinnedModelData& model, const std::string& modelRelativePath) {
  fs::path rel = modelRelativePath;
  rel.replace_extension(".sockets.json");
  std::string socketsRel = rel.generic_string();
  std::string socketsAbs = resolvePath(socketsRel);

  std::ifstream file(socketsAbs);
  if (!file.is_open()) {
    LOG_INFO("[Sockets] No sockets file for '{}' (looked for '{}')",
             modelRelativePath, socketsRel);
    return;
  }

  nlohmann::json j;
  try {
    file >> j;
  } catch (const nlohmann::json::exception& e) {
    LOG_ERROR("[Sockets] JSON parse error in '{}': {}", socketsRel, e.what());
    return;
  }

  if (!j.contains("sockets") || !j["sockets"].is_object()) {
    LOG_ERROR("[Sockets] '{}' missing top-level 'sockets' object", socketsRel);
    return;
  }

  // Build bone-name -> index lookup to start so we don't need
  // a linear skeleton scan per socket.
  std::unordered_map<std::string, int> boneIndex;
  boneIndex.reserve(model.skeleton.size());
  for (int i = 0; i < static_cast<int>(model.skeleton.size()); ++i) {
    boneIndex.emplace(model.skeleton[i].name, i);
  }

  int loaded = 0;
  int skipped = 0;
  for (auto it = j["sockets"].begin(); it != j["sockets"].end(); ++it) {
    const std::string& socketName = it.key();
    const auto& entry = it.value();

    // Required bone field (str)
    if (!entry.contains("bone") || !entry["bone"].is_string()) {
      LOG_ERROR("[Sockets] '{}' socket '{}' missing required 'bone' string",
                socketsRel, socketName);
      ++skipped;
      continue;
    }
    std::string boneName = entry["bone"].get<std::string>();

    // resolve bone name to skeleton index
    auto bi = boneIndex.find(boneName);
    if (bi == boneIndex.end()) {
      LOG_ERROR("[Sockets] '{}' socket '{}' → bone '{}' not found in skeleton "
                "(skeleton has {} bones)", socketsRel, socketName, boneName,
                model.skeleton.size());
      ++skipped;
      continue;
    }

    // Optional offset object. Individual sub-fields (pos/rot/scale)
    // are also optional and default to identity components
    glm::vec3 pos (0.0f, 0.0f, 0.0f);
    glm::vec3 rotD (0.0f, 0.0f, 0.0f);
    glm::vec3 scale (1.0f, 1.0f, 1.0f);

    if (entry.contains("offset") && entry["offset"].is_object()) {
      const auto& off = entry["offset"];

      auto readVec3 = [&](const char* key, glm::vec3& out) {
        if (!off.contains(key)) return;
        const auto& arr = off[key];
        if (!arr.is_array() || arr.size() != 3) {
          LOG_ERROR("[Sockets] '{}' socket '{}' offset.{} must be a 3-element "
                    "number array; using default", socketsRel, socketName, key);
          return;
        }
        out = glm::vec3(
          arr[0].get<float>(),
          arr[1].get<float>(),
          arr[2].get<float>()
        );
      };

      readVec3("pos", pos);
      readVec3("rot", rotD);
      readVec3("scale", scale);
    }

    // Compose local offset matrix as T * R * S to match
    // Transform::getModelMatrix()
    // Consistency matters, socket offset and Transform must use
    // same convention so authored values behave identically
    // in both places
    const glm::mat4 T = glm::translate(glm::mat4(1.0f), pos);
    const glm::mat4 R = glm::toMat4(glm::quat(glm::radians(rotD)));
    const glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
    const glm::mat4 localOffset = T * R * S;

    Socket socket;
    socket.boneIndex = bi->second;
    socket.localOffset = localOffset;
    model.sockets.emplace(socketName, socket);
    ++loaded;
  }

  LOG_INFO("[Sockets] '{}' loaded {} sockets ({} skipped)",
           modelRelativePath, loaded, skipped);
}
}
