#include <assimp/material.h>
#include <assimp/types.h>
#include <forge/AssetManager.h>
#include <forge/Logger.h>
#include <forge/Shader.h>
#include <forge/Mesh.h>

// Assimp
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <memory>
#include <string>

//stb_image
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <GL/glew.h>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace forge {

// Static members
std::string AssetManager::s_assetRoot;
std::unordered_map<std::string, ModelData> AssetManager::s_models;
std::unordered_map<std::string, std::shared_ptr<Shader>> AssetManager::s_shaders;
std::unordered_map<std::string, std::shared_ptr<Texture>> AssetManager::s_textures;

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

  // Merge all meshes in the file into one
  std::vector<Vertex> allVerts;
  std::vector<unsigned int> allIdx;
  unsigned int indexOffset = 0;

  for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
    aiMesh* aiM = scene->mMeshes[m];

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

      allVerts.push_back(v);
    }

    for (unsigned int f = 0; f < aiM->mNumFaces; f++) {
      const aiFace& face = aiM->mFaces[f];
      for (unsigned int j = 0; j < face.mNumIndices; j++) {
        allIdx.push_back(face.mIndices[j] + indexOffset);
      }
    }
    indexOffset += aiM->mNumVertices;
  }

  glm::vec3 bmin(1e9f), bmax(-1e9f);
  for (auto& v : allVerts) {
    glm::vec3 p(v.position[0], v.position[1], v.position[2]);
    bmin = glm::min(bmin, p);
    bmax = glm::max(bmax, p);
  }
  glm::vec3 size = bmax - bmin;
  LOG_INFO("[Assets] '{}' bounds  min:({:.2f},{:.2f},{:.2f})  max:({:.2f},{:.2f},{:.2f})  size:({:.2f},{:.2f},{:.2f})",
         path,
         bmin.x, bmin.y, bmin.z,
         bmax.x, bmax.y, bmax.z,
         size.x, size.y, size.z);

  LOG_INFO("[Assets] Model '{}' - {} verts, {} indices",
           path, allVerts.size(), allIdx.size());

  fs::path modelDir = fs::path(absPath).parent_path();
  std::shared_ptr<Texture> texture = nullptr;

  // Check the first mesh material for diffuse texture
  if (scene->mNumMeshes > 0 && scene->mNumMaterials > 0) {
    aiMesh* firstMesh = scene->mMeshes[0];
    aiMaterial* mat = scene->mMaterials[firstMesh->mMaterialIndex];

    aiString texPath;
    if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
      // Assimp gives us a path like "Textures\cobblestone.png"
      // Try several resolution strats in order:

      std::vector<fs::path> candidates = {
        //1. As is (may be absolute)
        fs::path(texPath.C_Str()),
        // 2. Relative to the models directory
        modelDir / texPath.C_Str(),
        // 3. Just the filename in the model directory
        modelDir / fs::path(texPath.C_Str()).filename(),
        // 4. Filename in a Textures/ subfolder of the model dir
        modelDir / "Textures" / fs::path(texPath.C_Str()).filename(),
      };

      for (auto& candidate : candidates) {
        std::string cStr = candidate.generic_string();
        if (fs::exists(candidate)) {
          LOG_INFO("[Assets] Texture resolved: {}", cStr);
          texture = loadTextureAbsolute(cStr);
          break;
        }
      }

      if (!texture)
        LOG_WARN("[Assets] Texture not found for '{}': {}", path, texPath.C_Str());
    }
  }

  ModelData result{ std::make_shared<Mesh>(allVerts, allIdx), texture };
  s_models[path] = result;
  return result;
}

void AssetManager::clear() {
  s_models.clear();
  s_textures.clear();
  s_shaders.clear();
  LOG_INFO("[Assets] Cache cleared");
}

void AssetManager::printStats() {
  LOG_INFO("[Assets] Cache: {} models, {} textures, {} Shaders cached",
           s_models.size(), s_textures.size(), s_shaders.size());
}

}
