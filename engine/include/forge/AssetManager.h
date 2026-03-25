#pragma once
#include <forge/Mesh.h>
#include <forge/Texture.h>
#include <forge/Shader.h>
#include <string>
#include <memory>
#include <unordered_map>

namespace forge {

struct ModelData {
  std::shared_ptr<Mesh> mesh;
  std::shared_ptr<Texture> texture; // Nullptr if no texture

  bool hasTexture() const { return texture != nullptr; }
};

class AssetManager {
public:
  static void setAssetRoot(const std::string& root);
  static const std::string& getAssetRoot();

  // Shaders
  // Key is "vert_path|frag_path"
  static std::shared_ptr<Shader> loadShader(
    const std::string& vertPath,
    const std::string& fragPath
  );

  static ModelData loadModel(const std::string& path);

  // Textures
  static std::shared_ptr<Texture> loadTexture(const std::string& path);

  static std::shared_ptr<Texture> loadTextureAbsolute(const std::string& absPath);

  // Release everything - call on shutdown or scene change
  static void clear();

  // Debug - log all currently cached assets
  static void printStats();
  static std::string resolvePath(const std::string& relativePath);

private:
  static std::string s_assetRoot;
  static std::unordered_map<std::string, ModelData> s_models;
  static std::unordered_map<std::string, std::shared_ptr<Shader>> s_shaders;
  static std::unordered_map<std::string, std::shared_ptr<Texture>> s_textures;
};

}
