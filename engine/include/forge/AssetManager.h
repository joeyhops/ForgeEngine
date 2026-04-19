#pragma once
#include <forge/Mesh.h>
#include <forge/SkinnedMesh.h>
#include <forge/SkinnedVertex.h>
#include <forge/Bone.h>
#include <forge/AnimationClip.h>
#include <forge/Texture.h>
#include <forge/Shader.h>
#include <forge/Socket.h>
#include <forge/WeaponDef.h>
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

namespace forge {

// Static Mesh
struct ModelData {
  struct SubMesh {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Texture> texture; // Nullptr if no texture
  };

  std::vector<SubMesh> subMeshes;

  std::shared_ptr<Mesh> mesh;
  std::shared_ptr<Texture> texture; // Nullptr if no texture

  std::vector<glm::vec3> positions;
  std::vector<uint32_t> indices;

  bool hasTexture() const { return texture != nullptr; }
  bool hasPhysicsData() const { return !positions.empty() && !indices.empty(); }
  bool hasRenderData() const { return !subMeshes.empty(); }
};

// Skinned Mesh
// Contains everything needed to make an animaor: the gpu mesh, the bone hierarchy and optional texture

struct SkinnedModelData {
  std::shared_ptr<SkinnedMesh> mesh;
  std::vector<Bone> skeleton; // Full bone hierarchy, parent indices set
  std::unordered_map<std::string, Socket> sockets;
  std::shared_ptr<Texture> texture;

  bool hasTexture() const { return texture != nullptr; }
  bool valid() const { return mesh != nullptr && !skeleton.empty(); }
};

class AssetManager {
public:
  static void setAssetRoot(const std::string& root);
  static const std::string& getAssetRoot();
  static std::string resolvePath(const std::string& relativePath);

  // Shaders
  // Key is "vert_path|frag_path"
  static std::shared_ptr<Shader> loadShader(
    const std::string& vertPath,
    const std::string& fragPath
  );

  // Static meshes
  static ModelData loadModel(const std::string& path);

  //Skinned mesh loading
  // Loads mesh geometry + skeleton from a GLB/FBX that contains an armature
  // Does NOT apply aiProcess_PreTransformVertices.
  static SkinnedModelData loadSkinnedModel(const std::string& path);

  // Animation clip
  // Load a single animation from a file
  static std::shared_ptr<AnimationClip> loadAnimationClip(
    const std::string& path,
    const std::string& clipName
  );

  // Textures
  static std::shared_ptr<Texture> loadTexture(const std::string& path);
  static std::shared_ptr<Texture> loadTextureAbsolute(const std::string& absPath);

  // Load weapon defs
  // reads weapons.json file and populates internal def table.
  // Called once at startup. Path is relative to asset root.
  static void loadWeponDefs(const std::string& relativePath);

  // returns nullptr if weaponId is not found. Does not log, caller logs
  static const WeaponDef* getWeaponDef(const std::string& weaponId);

  // Release everything - call on shutdown or scene change
  static void clear();
  static void printStats();

private:
  static void loadSocketsFor(SkinnedModelData& model, const std::string& modelRelativePath);

  static std::string s_assetRoot;
  static std::unordered_map<std::string, ModelData> s_models;
  static std::unordered_map<std::string, SkinnedModelData> s_skinnedModels;
  static std::unordered_map<std::string, std::shared_ptr<AnimationClip>> s_clips;
  static std::unordered_map<std::string, std::shared_ptr<Shader>> s_shaders;
  static std::unordered_map<std::string, std::shared_ptr<Texture>> s_textures;
  static std::unordered_map<std::string, WeaponDef> s_weaponDefs;
};

}
