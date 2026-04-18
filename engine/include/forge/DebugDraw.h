#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace forge {
class Shader;

// DebugDraw
// Static immediate mode renderer for physics debugging
// Shapes are queued with capsule(), box(), line()
// then flush()ed and drawn all at once
//
// Frame Usage (in ForgeGame::onRender, after geometry, before DebugUI::end):
// if (getDebugUI().isPhysicsEnabled()) {
//  forge::DebugDraw::capsule(center, radius, cylinderHalfHeight, {0,1,0});
//  forge::DebugDraw::flush(m_camera->getViewProjection());
// }
//
// init() must be called once after the OpenGL context is ready
// shutdown() must be called before the context is destroyed
//
// all pos in world space

class DebugDraw {
public:
  static void init(Shader* lineShader);
  static void shutdown();

  static void capsule(const glm::vec3& center,
                      float radius,
                      float cylinderHalfHeight,
                      const glm::vec3& color = { 0.0f, 1.0f, 0.0f });

  static void box(const glm::vec3& center,
                  const glm::vec3& halfExtents,
                  const glm::vec3& color = { 1.0f, 1.0f, 1.0f });

  static void line(const glm::vec3& a, const glm::vec3& b,
                   const glm::vec3& color = { 1.0f, 1.0f, 1.0f });

  static void flush(const glm::mat4& viewProjection);

private:
  struct Vertex { glm::vec3 position; };

  // Per-draw command batch: batch by color
  struct DrawCommand {
    std::vector<Vertex> vertices; // GL_LINES: pairs of vertices
    glm::vec3 color;
  };

  static void pushLine(const glm::vec3& a, const glm::vec3& b, const glm::vec3& color);

  // gen horizonal circle in XZ plane at given Y with n segments
  static void pushCircleXZ(const glm::vec3& center, float radius,
                           const glm::vec3& color, int segments = 16);

  // generate a vertical arc from angle startRad to endRad (radians) in the XY plane.
  static void pushArcXY(const glm::vec3& center, float radius,
                        float startRad, float endRad,
                        const glm::vec3& color, int segments = 8);

  // Generate a vertical arc in the ZY plane
  static void pushArcZY(const glm::vec3& center, float radius,
                        float startRad, float endRad,
                        const glm::vec3& color, int segments = 8);

  static Shader* s_shader;
  static std::vector<DrawCommand> s_commands;
  static uint32_t s_vao;
  static uint32_t s_vbo;
  static bool s_initialized;
};
}
