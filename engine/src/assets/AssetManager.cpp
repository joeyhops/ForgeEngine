#include <assimp/material.h>
#include <forge/AssetManager.h>
#include <forge/Logger.h>
#include <forge/Shader.h>
#include <forge/Mesh.h>
#include <forge/SkinnedMesh.h>
#include <forge/SkinnedVertex.h>
#include <forge/Bone.h>
#include <forge/AnimationClip.h>
#include <forge/TAESerialization.h>
#include <forge/Material.h>

// Assimp
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h>
#include <map>
#include <memory>
#include <string>
#include <functional>
#include <unordered_map>
#include <unordered_set>

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
std::unordered_map<std::string, MovesetDef> AssetManager::s_movesetDefs;
std::unordered_map<std::string, std::shared_ptr<Material>> AssetManager::s_materials;

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

// Helper:
static void applyMaterialJSON(forge::Material& mat, const nlohmann::json& j) {
  if (j.contains("albedo"))
    mat.albedo = forge::AssetManager::loadTexture(j["albedo"].get<std::string>());
  if (j.contains("normalMap"))
    mat.normalMap = forge::AssetManager::loadTexture(j["normalMap"].get<std::string>());
  if (j.contains("metallicMap"))
    mat.metallicMap = forge::AssetManager::loadTexture(j["metallicMap"].get<std::string>());
  if (j.contains("roughnessMap"))
    mat.roughnessMap = forge::AssetManager::loadTexture(j["roughnessMap"].get<std::string>());
  if (j.contains("aoMap"))
    mat.aoMap = forge::AssetManager::loadTexture(j["aoMap"].get<std::string>());
  if (j.contains("albedoTint")) {
    auto& a = j["albedoTint"];
    mat.albedoTint = glm::vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>());
  }
  if (j.contains("metallic"))
    mat.metallic = j["metallic"].get<float>();
  if (j.contains("roughness"))
    mat.roughness = j["roughness"].get<float>();
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
  if (rel.empty()) return s_assetRoot;

  // Already absolute (Unix /... or Windows C:\...) — pass through untouched.
  if (rel[0] == '/' || (rel.size() > 1 && rel[1] == ':'))
    return rel;

  // Path already starts with the asset root prefix — don't double it.
  // This happens when users type paths like "assets/anim/..." in the TAE editor
  // while the asset root is also "assets/".
  if (!s_assetRoot.empty() && rel.rfind(s_assetRoot, 0) == 0)
    return rel;

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

  s_textures[key] = tex;
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
    aiProcess_CalcTangentSpace      |   // Populates mTangents[] and mBitangents[]
    aiProcess_FlipUVs               |   // FBX/GLTF UV origin is top-left; OpenGL is bottom-left
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

      if (aiM->HasTangentsAndBitangents()) {
        glm::vec3 T(aiM->mTangents[i].x, aiM->mTangents[i].y, aiM->mTangents[i].z);
        glm::vec3 B(aiM->mBitangents[i].x, aiM->mBitangents[i].y, aiM->mBitangents[i].z);
        glm::vec3 N(v.normal[0], v.normal[1], v.normal[2]);
        float handedness = (glm::dot(glm::cross(N, T), B) < 0.0f) ? -1.0f : 1.0f;
        v.tangent[0] = T.x; v.tangent[1] = T.y; v.tangent[2] = T.z;
        v.tangent[3] = handedness;
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

    if (matIndex < scene->mNumMaterials) {
      aiMaterial* aiMat = scene->mMaterials[matIndex];

      aiString matName;
      aiMat->Get(AI_MATKEY_NAME, matName);
      sub.name = matName.C_Str();

      // Load textures from FBX/GLTF material data. The .mat.json sidecar
      // loaded below can override any of these on a per-material basis.
      auto tryLoadTex = [&](aiTextureType texType) -> std::shared_ptr<Texture> {
        aiString texPath;
        if (aiMat->GetTexture(texType, 0, &texPath) != AI_SUCCESS) return nullptr;
        std::string tp = texPath.C_Str();
        if (tp.empty()) return nullptr;

        // Embedded texture — index prefixed with '*' (common in GLB files)
        if (tp[0] == '*') {
          int embIdx = 0;
          try { embIdx = std::stoi(tp.substr(1)); } catch (...) { return nullptr; }
          if (embIdx >= 0 && embIdx < (int)scene->mNumTextures) {
            std::string cacheKey = absPath + "[embedded:" + std::to_string(embIdx) + "]";
            auto cit = s_textures.find(cacheKey);
            if (cit != s_textures.end()) return cit->second;

            const aiTexture* emb = scene->mTextures[embIdx];
            if (emb->mHeight == 0) {
              stbi_set_flip_vertically_on_load_thread(true);
              int w, h, ch;
              unsigned char* data = stbi_load_from_memory(
                reinterpret_cast<const unsigned char*>(emb->pcData),
                emb->mWidth, &w, &h, &ch, 0);
              if (data) {
                GLenum fmt = (ch == 4) ? GL_RGBA : GL_RGB;
                auto tex = std::make_shared<Texture>();
                tex->width = w; tex->height = h; tex->path = cacheKey;
                glGenTextures(1, &tex->id);
                glBindTexture(GL_TEXTURE_2D, tex->id);
                glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
                glGenerateMipmap(GL_TEXTURE_2D);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                glBindTexture(GL_TEXTURE_2D, 0);
                stbi_image_free(data);
                s_textures[cacheKey] = tex;
                return tex;
              }
            }
          }
          return nullptr;
        }

        // External texture file: search several candidate locations.
        // FBX files often embed absolute Windows paths — strip to filename and
        // check relative to the model directory and a conventional Textures/ subdir.
        std::vector<fs::path> candidates = {
          fs::path(tp),
          modelDir / tp,
          modelDir / fs::path(tp).filename(),
          modelDir / "Textures" / fs::path(tp).filename(),
        };
        for (auto& cand : candidates) {
          if (fs::exists(cand))
            return loadTextureAbsolute(cand.generic_string());
        }
        return nullptr;
      };

      sub.material.albedo = tryLoadTex(aiTextureType_DIFFUSE);
      if (!sub.material.albedo)
        sub.material.albedo = tryLoadTex(aiTextureType_BASE_COLOR);
      sub.material.normalMap = tryLoadTex(aiTextureType_NORMALS);
      if (!sub.material.normalMap)
        sub.material.normalMap = tryLoadTex(aiTextureType_HEIGHT);
      sub.material.metallicMap = tryLoadTex(aiTextureType_METALNESS);
      sub.material.roughnessMap = tryLoadTex(aiTextureType_DIFFUSE_ROUGHNESS);
      sub.material.aoMap = tryLoadTex(aiTextureType_AMBIENT_OCCLUSION);
      if (!sub.material.aoMap)
        sub.material.aoMap = tryLoadTex(aiTextureType_LIGHTMAP);
    }

    result.subMeshes.push_back(std::move(sub));
  }

  if (!result.subMeshes.empty())
    result.mesh = result.subMeshes[0].mesh;

  {
    std::string sidecarAbs = resolvePath(path + ".mat.json");
    std::ifstream sf(sidecarAbs);
    if (sf.is_open()) {
      try {
        nlohmann::json j = nlohmann::json::parse(sf);
        if (j.contains("materials")) {
          for (auto& sub : result.subMeshes)
            if (j["materials"].contains(sub.name))
              applyMaterialJSON(sub.material, j["materials"][sub.name]);
        } else {
          for (auto& sub : result.subMeshes)
            applyMaterialJSON(sub.material, j);
        }
        LOG_INFO("[Assets] Applied material sidecar: {}.mat.json", path);
      } catch (const nlohmann::json::exception& e) {
        LOG_WARN("[Assets] Material sidecar parse error for {}: {}", path, e.what());
      }
    }
  }

  s_models[path] = result;
  return result;
}

SkinnedModelData AssetManager::loadSkinnedModel(const std::string& path) {
  auto it = s_skinnedModels.find(path);
  if (it != s_skinnedModels.end()) return it->second;

  std::string absPath = resolvePath(path);

  Assimp::Importer importer;
  importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
  importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_OPTIMIZE_EMPTY_ANIMATION_CURVES, false);
  const unsigned int flags =
    aiProcess_Triangulate | // Quads -> triangles
    aiProcess_GenSmoothNormals | // gen normals if missing
    aiProcess_CalcTangentSpace |
    aiProcess_FlipUVs | // OpenGL UV origin = btm-left
    aiProcess_JoinIdenticalVertices | // de dupe
    aiProcess_LimitBoneWeights; // clamp to 4 influences per vertex

  const aiScene* scene = importer.ReadFile(absPath, flags);
  if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
    throw std::runtime_error("[Assets] loadSkinnedModel failed for '" + path + "': " + importer.GetErrorString());

  // Build Skeleton from scene graph in topological order (DFS):
  //
  // Two passes plus final DFS walk:
  //    Pass 1: collect skin bones (/w vertex weights) and offset
  //    matrices from aiMesh->mBones across all meshes
  //
  //    pass 2: Mark every scene-graph node that is itself a skin 
  //    bone or has any descendants this is. This add STRUCTURAL
  //    bones (e.g. UE5 Manny's thigh_r/calf_r - pure parents
  //    with no weights, bu thier childrens bind rotations need
  //    to cascade through them) to the bone set
  //
  //    pass 3: walk the scene graph in DFS order. Each marked node
  //    gets the next sequential engine index. DFS gurantees 
  //    parent_index < own_index for every bone which is what
  //    Animator::computeBoneMatrices single-pass iteration requires
  //
  // The old code (which iterated over aiMesh->mBones for both bone set
  // and index assignment) silently produced wrong results for skeletons
  // that violate either of these properties.

  // Pass 1 - skins bones /w offset matrices, keyed by name
  std::unordered_map<std::string, glm::mat4> skinBoneOffsets;
  for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
    aiMesh* aiM = scene->mMeshes[m];
    for (unsigned int b = 0; b < aiM->mNumBones; b++) {
      aiBone* bone = aiM->mBones[b];
      std::string bname(bone->mName.C_Str());
      if (skinBoneOffsets.find(bname) == skinBoneOffsets.end())
        skinBoneOffsets[bname] = aiMat4ToGlm(bone->mOffsetMatrix);
    }
  }

  // Pass 2 - mark all bone nodes (skin bones + structural ancestors)
  std::unordered_set<std::string> boneNodeNames;
  std::function<bool(aiNode*)> markBones;
  markBones = [&](aiNode* node) -> bool {
    std::string nodeName(node->mName.C_Str());
    bool subtreeHasBone = (skinBoneOffsets.count(nodeName) > 0);
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
      if (markBones(node->mChildren[i])) {
        subtreeHasBone = true;
      }
    }
    if (subtreeHasBone) {
      boneNodeNames.insert(nodeName);
    }
    return subtreeHasBone;
  };
  markBones(scene->mRootNode);

  // Pass 2b - extend bone-set to include attachment only
  // descendants of already marked bones. These leaves don't
  // get caught by Pass 2's "has skin-bone descendants" rule
  // because they have no descendants, but they belong in the
  // skeleton so socket based systems (EquipmentComponent,
  // IK based systems) can find them by name
  std::function<void(aiNode*, bool)> propagateDown;
  propagateDown = [&](aiNode* node, bool insideSkeleton) {
    std::string nodeName(node->mName.C_Str());
    bool isMeshCarrier = (node->mNumMeshes > 0);
    if (boneNodeNames.count(nodeName)) {
      insideSkeleton = true;
    } else if (insideSkeleton && !isMeshCarrier){
      boneNodeNames.insert(nodeName);
    }
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
      propagateDown(node->mChildren[i], insideSkeleton);
    }
  };
  propagateDown(scene->mRootNode, false);

  // Pass 3 - DFS index assignment + skeleton population
  // Skip the scene root itself (it carries the mesh, not
  // a deformation transform); start the recursion at its
  // children. Top level bones therefore recieve parentIndex
  // = -1
  std::unordered_map<std::string, int> boneNameToIndex;
  std::vector<Bone> skeleton;
  std::function<void(aiNode*, int)> assignIndices;
  assignIndices = [&](aiNode* node, int parentBoneIdx) {
    std::string nodeName(node->mName.C_Str());
    int thisBoneIdx = parentBoneIdx;
    if (boneNodeNames.count(nodeName)) {
      thisBoneIdx = static_cast<int>(skeleton.size());
      boneNameToIndex[nodeName] = thisBoneIdx;
      Bone bone;
      bone.name = nodeName;
      bone.parentIndex = parentBoneIdx;
      bone.localTransform = aiMat4ToGlm(node->mTransformation);
      auto it = skinBoneOffsets.find(nodeName);
      bone.offsetMatrix = (it != skinBoneOffsets.end()) ? it->second : glm::mat4(1.0f);
      skeleton.push_back(std::move(bone));
    }
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
      assignIndices(node->mChildren[i], thisBoneIdx);
    }
  };
  for (unsigned int i = 0; i < scene->mRootNode->mNumChildren; i++) {
    assignIndices(scene->mRootNode->mChildren[i], -1);
  }

  int boneCount = static_cast<int>(skeleton.size());
  LOG_INFO("[Assets] '{}' - {} bones ({} skin-weighted, {} structural)",
           path, boneCount, static_cast<int>(skinBoneOffsets.size()),
           boneCount - static_cast<int>(skinBoneOffsets.size()));

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
      std::string bname(bone->mName.C_Str());
      auto it = boneNameToIndex.find(bname);
      if (it == boneNameToIndex.end()) {
        LOG_WARN("[Assets] '{}' skinned to unregistered bone '{}', skipping weights",
                 path, bname);
        continue;
      }
      int boneIdx = it->second;
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

      if (aiM->HasTangentsAndBitangents()) {
        glm::vec3 T(aiM->mTangents[i].x, aiM->mTangents[i].y, aiM->mTangents[i].z);
        glm::vec3 B(aiM->mBitangents[i].x, aiM->mBitangents[i].y, aiM->mBitangents[i].z);
        glm::vec3 N(v.normal[0], v.normal[1], v.normal[2]);
        float handedness = (glm::dot(glm::cross(N, T), B) < 0.0f) ? -1.0f : 1.0f;
        v.tangent[0] = T.x; v.tangent[1] = T.y; v.tangent[2] = T.z;
        v.tangent[3] = handedness;
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
  result.material.albedo = texture;

  // Apply single material sidecar if present (skinned models are single-surface)
  {
    std::string sidecarAbs = resolvePath(path + ".mat.json");
    std::ifstream sf(sidecarAbs);
    if (sf.is_open()) {
      try {
        nlohmann::json j = nlohmann::json::parse(sf);
        applyMaterialJSON(result.material, j);
      } catch (const nlohmann::json::exception& e) {
        LOG_WARN("[Assets] Material sidecar parse error for {}: {}", path, e.what());
      }
    }
  }

  s_skinnedModels[path] = result;
  return result;
}

std::shared_ptr<AnimationClip> AssetManager::loadAnimationClip(
    const std::string& path,
    const std::string& clipName,
    const std::string& taeKey)
{
  // Cache key combines path + clip name so the same file can yield
  // multiple named clips without reloading.
  std::string cacheKey = path + "|" + clipName;
  if (!taeKey.empty()) cacheKey += "|" + taeKey;

  auto it = s_clips.find(cacheKey);
  if (it != s_clips.end()) return it->second;
 
  std::string absPath = resolvePath(path);
 
  // For animation-only loading we skip mesh processing entirely.
  Assimp::Importer importer;
  importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
  importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_OPTIMIZE_EMPTY_ANIMATION_CURVES, false);
  const aiScene* scene = importer.ReadFile(absPath, aiProcess_PopulateArmatureData | aiProcess_GlobalScale);
  if (!scene) {
    LOG_ERROR("[Assets] Assimp failed to open '{}': {}", path, importer.GetErrorString());
    return nullptr;
  }

  if (scene->mNumAnimations == 0) {
    LOG_WARN("[Assets] '{}' parsed OK but contains 0 animation tracks. "
           "File may be mesh-only, or the FBX animation layer uses an "
           "unsupported format.", path);
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

  fs::path clipFsPath(path);
  std::string sidecarStem = taeKey.empty() ? clipFsPath.stem().string() : taeKey;
  std::string sidecarRel = (clipFsPath.parent_path()
                            / "tae"
                            / (sidecarStem + ".tae.json"))
    .generic_string();

  std::string sidecarAbs = resolvePath(sidecarRel);

  if (fs::exists(sidecarAbs)) {
    std::string rootBone;
    bool extractY = false;
    clip->events = TAESerialization::load(sidecarAbs, rootBone, extractY);
    if (!rootBone.empty()) {
      clip->useRootMotion = true;
      clip->rootBoneName = rootBone;
      clip->extractRootY = extractY;
    }
  }
  s_clips[cacheKey] = clip;
  return clip;
}

void AssetManager::loadWeaponDefs(const std::string& relativePath) {
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

  auto parseEntry = [](const nlohmann::json& entry) {
    WeaponDef def;
    def.id = entry.at("id").get<std::string>();
    def.meshPath = entry.value("mesh", "");

    if (entry.contains("boneAttach"))
      def.boneAttach = entry.at("boneAttach").get<std::string>();
    else
      throw std::runtime_error(
        "WeaponDef '" + def.id + "': missing boneAttach");

    if (entry.contains("meshOffset")) {
      const auto& mo = entry.at("meshOffset");
      glm::vec3 pos(0.0f), scale(1.0f);
      glm::quat rot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

      if (mo.contains("pos")) {
        auto& p = mo.at("pos");
        pos = { p[0], p[1], p[2] };
      }
      if (mo.contains("rot")) {
        auto& r = mo.at("rot");
        glm::vec3 euler(r[0], r[1], r[2]);
        rot = glm::quat(glm::radians(euler));
      }
      if (mo.contains("scale")) {
        auto& s = mo.at("scale");
        scale = { s[0], s[1], s[2] };
      }

      glm::mat4 T = glm::translate(glm::mat4(1.0f), pos);
      glm::mat4 R = glm::mat4_cast(rot);
      glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
      def.meshOffset = T * R * S;
    }

    def.movesetId = entry.value("movesetId", "");
    def.weaponArtId = entry.value("weaponArtId", "");

    for (const auto& [atkName, atkData] : entry.at("attacks").items()) {
      AttackData atk;
      atk.name = atkName;
      atk.damage = atkData.value("damage", atk.damage);
      atk.poiseDamage = atkData.value("poiseDamage", atk.poiseDamage);
      atk.staminaCost = atkData.value("staminaCost", atk.staminaCost);
      atk.damageType = atkData.value("damageType", atk.damageType);
      def.attacks[atkName] = atk;
    }

    return def;
  };

  for (const auto& w : j.at("weapons")) {
    nlohmann::json entryJson = w;

    if (w.contains("file")) {
      std::string extPath = resolvePath(w.at("file").get<std::string>());
      std::ifstream extFile(extPath);
      if (!extFile.is_open()) {
        LOG_ERROR("[AssetManager] loadWeaponDefs: Could not open '{}'", extPath);
        continue;
      }
      try { extFile >> entryJson; }
      catch (nlohmann::json::exception& e) {
        LOG_ERROR("[AssetManager] loadWeaponDefs: parse error in '{}': {}", extPath, e.what());
        continue;
      }
    }

    WeaponDef def = parseEntry(entryJson);
    std::string defId = def.id;
    s_weaponDefs[defId] = std::move(def);
    LOG_INFO("[AssetManager] Loaded weapon def '{}'", defId);
  }

  LOG_INFO("[AssetManager] loadedWeaponDefs: {} weapons loaded from '{}'", s_weaponDefs.size(), relativePath);
}

const WeaponDef* AssetManager::getWeaponDef(const std::string& weaponId) {
  auto it = s_weaponDefs.find(weaponId);
  return (it != s_weaponDefs.end()) ? &it->second : nullptr;
}

void AssetManager::loadMovesetDefs(const std::string& relativePath) {
  std::string fullPath = resolvePath(relativePath);
  std::ifstream file(fullPath);
  if (!file.is_open()) {
    LOG_ERROR("[AssetManager] loadMovesetDefs: cannot open '{}'", fullPath);
    return;
  }

  nlohmann::json j;
  try {
    file >> j;
  } catch (const nlohmann::json::exception& e) {
    LOG_ERROR("[AssetManager] loadMovesetDefs: JSON parse error: {}", e.what());
    return;
  }

  auto parseEntry = [](const nlohmann::json& entry) {
    MovesetDef def;
    def.id = entry.at("id").get<std::string>();

    if (entry.contains("clips")) {
      for (const auto& [key, path] : entry.at("clips").items())
        def.clips[key] = path.get<std::string>();
    }
    return def;
  };

  for (const auto& m : j.at("movesets")) {
    nlohmann::json entryJson = m; 

    if (m.contains("file")) {
      std::string extPath = resolvePath(m.at("file").get<std::string>());
      std::ifstream extFile(extPath);
      if (!extFile.is_open()) {
        LOG_ERROR("[AssetManager] loadMovesetDefs: Could not open '{}'", extPath);
        continue;
      }
      try { extFile >> entryJson; }
      catch (nlohmann::json::exception& e) {
        LOG_ERROR("[AssetManager] loadMovesetDefs: parse error in '{}': {}", extPath, e.what());
        continue;
      }
    }

    MovesetDef def = parseEntry(entryJson);
    std::string defId = def.id;
    s_movesetDefs[defId] = std::move(def);
    LOG_INFO("[AssetManager] Loaded Moveset Def {} ({} clips)", defId, s_movesetDefs[defId].clips.size());
  }
  LOG_INFO("[AssetManager] loadMovesetDefs: {} movesets loaded from '{}'",
           s_movesetDefs.size(), relativePath);
}

const MovesetDef* AssetManager::getMovesetDef(const std::string& movesetId) {
  auto it = s_movesetDefs.find(movesetId);
  return (it != s_movesetDefs.end()) ? &it->second : nullptr;
}

const std::unordered_map<std::string, WeaponDef>& AssetManager::getAllWeaponDefs() {
  return s_weaponDefs;
}

const std::unordered_map<std::string, MovesetDef>& AssetManager::getAllMovesetDefs() {
  return s_movesetDefs;
}

std::shared_ptr<Material> AssetManager::loadMaterial(const std::string& relativePath) {
  auto it = s_materials.find(relativePath);
  if (it != s_materials.end()) return it->second;

  std::string absPath = resolvePath(relativePath);
  std::ifstream f(absPath);
  if (!f.is_open()) {
    LOG_WARN("[Assets] Material not found: {}", absPath);
    return nullptr;
  }

  nlohmann::json j;
  try {
    j = nlohmann::json::parse(f);
  } catch (const nlohmann::json::exception& e) {
    LOG_ERROR("[Assets] Material JSON parse error in {}: {}", absPath, e.what());
    return nullptr;
  }

  auto mat = std::make_shared<Material>();
  applyMaterialJSON(*mat, j);

  s_materials[relativePath] = mat;
  LOG_INFO("[Assets] Loaded material: {}", relativePath);
  return mat;
}

std::shared_ptr<Material> AssetManager::loadMaterialForMesh(const std::string& meshRelativePath) {
  return loadMaterial(meshRelativePath + ".mat.json");
}

std::shared_ptr<Material> AssetManager::loadMaterialForTBTexture(const std::string& bareName) {
  std::string cacheKey = "__tb__" + bareName;
  auto it = s_materials.find(cacheKey);
  if (it != s_materials.end()) return it->second;

  // Try explicit mat.json sidecar first
  std::string sidecarPath = "texture/" + bareName + ".mat.json";
  std::ifstream probe(resolvePath(sidecarPath));
  std::shared_ptr<Material> mat;

  if (probe.is_open()) {
    probe.close();
    mat = loadMaterial(sidecarPath);
  } else {
    // fallback
    mat = std::make_shared<Material>();
    mat->albedo = loadMapTexture(bareName);
    mat->normalMap = loadMapNormalTexture(bareName);
    mat->roughnessMap = loadMapRoughnessTexture(bareName);
    mat->metallicMap = loadMapMetallicTexture(bareName);
    mat->aoMap = loadMapAOTexture(bareName);
    LOG_INFO("[Assets] Built material from naming convention: {}", bareName);
  }

  s_materials[cacheKey] = mat;
  return mat;
}

void AssetManager::clear() {
  s_skinnedModels.clear();
  s_clips.clear();
  s_models.clear();
  s_textures.clear();
  s_shaders.clear();
  s_materials.clear();
  s_weaponDefs.clear();
  s_movesetDefs.clear();
  LOG_INFO("[Assets] Cache cleared");
}

void AssetManager::printStats() {
  LOG_INFO("[Assets] Cache: {} models, {} textures, {} Shaders, {} materials cached",
           s_models.size(), s_textures.size(), s_shaders.size(), s_materials.size());
}

}
