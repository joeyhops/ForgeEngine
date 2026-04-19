#include <forge/map/MapScene.h>
#include <GL/glew.h>

namespace forge {

void MapScene::render(const Camera& camera, Shader* brushShader) const {
  if (!brushShader) return;

  brushShader->bind();
  glm::mat4 model = glm::mat4(1.0f); // Map geom static at origin
  glm::mat4 mvp = camera.getViewProjection() * model;

  brushShader->setMat4("u_mvp", mvp);
  brushShader->setMat4("u_model", model);

  // Simple directional light from exisitng basic shader logic
  brushShader->setVec3("u_lightDir", glm::normalize(glm::vec3(0.6f, 1.0f, 0.4f)));

  for (const auto& obj : renderObjects) {
    if (!obj.mesh) continue;

    if (obj.albedo) {
      obj.albedo->bind(0);
      brushShader->setInt("u_albedo", 0);
      brushShader->setBool("u_hasAlbedo", true);
    } else {
      brushShader->setBool("u_hasAlbedo", false);
    }

    if (obj.normalMap) {
      obj.normalMap->bind(1);
      brushShader->setInt("u_normalMap", 1);
      brushShader->setBool("u_hasNormalMap", true);
    } else {
      if (obj.albedo) obj.albedo->bind(1);
      brushShader->setInt("u_normalMap", 1);
      brushShader->setBool("u_hasNormalMap", false);
    }

    obj.mesh->draw();
  }
  brushShader->unbind();
}

}
