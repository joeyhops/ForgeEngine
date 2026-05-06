#pragma once
#include <forge/AssetManager.h>
#include <forge/Camera.h>
#include <forge/Material.h>
#include <forge/Shader.h>

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace forge {

class MapScene;

class Renderer {
public:
  explicit Renderer(const std::string& shaderRoot);
  ~Renderer() = default;

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  // Frame lifecycle
  // Call first in onRender(). Clears color and depth as well
  // as caches the VP matrix and camera world pos for curr frame
  // Every subsequent draw call after beginFrame() uses said
  // cached values, meaning no camera reference needed
  // at draw time
  void beginFrame(const Camera& camera, int viewportW, int viewportH);
  // Tool variant: clears color and depth without caching a camera
  // Use when the caller manages its own VP matrix independently
  // (e.g. TAE Editor, other headless tool)
  void beginFrame(int viewportW, int viewportH);

  // Call after all draw calls
  void endFrame();

  // Draw submission

  // Static mesh
  void drawMesh(const ModelData& model, const glm::mat4& modelMatrix);

  // Skinned mesh
  void drawSkinnedMesh(const SkinnedModelData& model,
                       const glm::mat4& modelMatrix,
                       const std::vector<glm::mat4>& boneMatrices);

  // Level geomertry
  void drawBrushScene(const MapScene& scene);

  Shader* getDebugLineShader() const { return m_debugLineShader.get(); }

  void setClearColor(float r, float g, float b);

private:
  // prepends platform specific directory to a shader filename
  std::string resolvePath(const std::string& name) const;

  std::shared_ptr<Shader> m_meshShader;
  std::shared_ptr<Shader> m_skinnedShader;
  std::shared_ptr<Shader> m_brushShader;
  std::shared_ptr<Shader> m_debugLineShader;

  glm::mat4 m_viewProj = glm::mat4(1.0f);
  glm::vec3 m_cameraPos = glm::vec3(0.0f);

  float m_clearColor[3] = { 0.08f, 0.08f, 0.10f };
};

}
