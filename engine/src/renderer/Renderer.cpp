#include <forge/Renderer.h>
#include <forge/Material.h>
#include <forge/map/MapScene.h>
#include <forge/Animator.h>
#include <forge/Logger.h>

#include <GL/glew.h>

#include <algorithm>

namespace forge {

Renderer::Renderer(const std::string& shaderRoot) {
  m_meshShader = AssetManager::loadShader(resolvePath("basic.vert"),
                                          resolvePath("basic.frag"));
  m_skinnedShader = AssetManager::loadShader(resolvePath("skinned.vert"),
                                          resolvePath("basic.frag"));
  m_brushShader = AssetManager::loadShader(resolvePath("brush.vert"),
                                          resolvePath("brush.frag"));
  m_debugLineShader = AssetManager::loadShader(resolvePath("debug_line.vert"),
                                          resolvePath("debug_line.frag"));

  LOG_INFO("[Renderer] Shaders loaded (root: {})", shaderRoot);
}

std::string Renderer::resolvePath(const std::string& name) const {
#ifdef __APPLE__
  return "shaders/mac/" + name;
#else
  return "shaders/win/" + name;
#endif
}

void Renderer::beginFrame(const Camera& camera, int /*viewportW*/, int /*viewportH*/) {
  glClearColor(m_clearColor[0], m_clearColor[1], m_clearColor[2], 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  m_viewProj = camera.getViewProjection();
  m_cameraPos = camera.getPosition();
}

void Renderer::beginFrame(int /*viewportW*/, int /*viewportH*/) {
  glClearColor(m_clearColor[0], m_clearColor[1], m_clearColor[2], 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::endFrame() {
  // No-op - see phase 18
}

void Renderer::setClearColor(float r, float g, float b) {
  m_clearColor[0] = r;
  m_clearColor[1] = g;
  m_clearColor[2] = b;
}

void Renderer::drawMesh(const ModelData& model, const glm::mat4& modelMatrix) {
  if (!model.hasRenderData()) return;

  m_meshShader->bind();

  glm::mat4 mvp = m_viewProj * modelMatrix;
  m_meshShader->setMat4("u_mvp", mvp);
  m_meshShader->setMat4("u_model", modelMatrix);

  for (const auto& sub : model.subMeshes) {
    if (!sub.mesh) continue;
    sub.material.bind(*m_meshShader, 0);
    sub.mesh->draw();
  }

  m_meshShader->unbind();
}

void Renderer::drawSkinnedMesh(const SkinnedModelData& model,
                               const glm::mat4& modelMatrix,
                               const std::vector<glm::mat4>& boneMatrices)
{
  if (!model.mesh) return;

  m_skinnedShader->bind();

  glm::mat4 mvp = m_viewProj * modelMatrix;
  m_skinnedShader->setMat4("u_mvp", mvp);
  m_skinnedShader->setMat4("u_model", modelMatrix);

  if (!boneMatrices.empty()) {
    m_skinnedShader->setBool("u_hasBones", true);
    m_skinnedShader->setMat4Array(
      "u_boneMatrices",
      static_cast<int>(std::min(boneMatrices.size(), (size_t)MAX_BONES)),
      boneMatrices.data()
    );
  } else {
    m_skinnedShader->setBool("u_hasBones", false);
  }

  model.material.bind(*m_skinnedShader, 0);
  model.mesh->draw();
  m_skinnedShader->unbind();
}

void Renderer::drawBrushScene(const MapScene& scene) {
  m_brushShader->bind();

  glm::mat4 model = glm::mat4(1.0f);
  m_brushShader->setMat4("u_mvp", m_viewProj * model);
  m_brushShader->setMat4("u_model", model);
  m_brushShader->setVec3("u_lightDir", glm::normalize(glm::vec3(0.6f, 1.0f, 0.4f)));
  m_brushShader->setVec3("u_lightColor", glm::vec3(1.0f));
  m_brushShader->setVec3("u_cameraPos", m_cameraPos);

  for (const auto& obj : scene.renderObjects) {
    if (!obj.mesh) continue;
    obj.material.bind(*m_brushShader, 0);
    obj.mesh->draw();
  }

  m_brushShader->unbind();
}

}
