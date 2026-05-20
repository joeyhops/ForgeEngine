#include <forge/DebugDraw.h>
#include <forge/Shader.h>
#include <forge/Logger.h>

#include <GL/glew.h>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace forge {

//Static members
Shader* DebugDraw::s_shader = nullptr;
std::vector<DebugDraw::DrawCommand> DebugDraw::s_commands;
uint32_t DebugDraw::s_vao = 0;
uint32_t DebugDraw::s_vbo = 0;
bool DebugDraw::s_initialized = false;

// Lifecycle

void DebugDraw::init(Shader* lineShader) {
  if (s_initialized) return;
  s_shader = lineShader;

  glGenVertexArrays(1, &s_vao);
  glGenBuffers(1, &s_vbo);

  glBindVertexArray(s_vao);
  glBindBuffer(GL_ARRAY_BUFFER, s_vbo);

  // Position-only vertices at location 0 - matche debug_line.vert
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void*>(offsetof(Vertex, position)));

  glBindVertexArray(0);

  s_initialized = true;
  LOG_INFO("[DebugDraw] Initialized");
}

void DebugDraw::shutdown() {
  if (!s_initialized) return;
  glDeleteBuffers(1, &s_vbo);
  glDeleteVertexArrays(1, &s_vao);
  s_commands.clear();
  s_initialized = false;
  LOG_INFO("[DebugDraw] Shut down");
}

void DebugDraw::pushLine(const glm::vec3& a, const glm::vec3& b, const glm::vec3& color) {
  for (auto& cmd : s_commands) {
    if (cmd.color == color) {
      cmd.vertices.push_back({ a });
      cmd.vertices.push_back({ b });
      return;
    }
  }

  DrawCommand cmd;
  cmd.color = color;
  cmd.vertices.push_back({ a });
  cmd.vertices.push_back({ b });
  s_commands.push_back(std::move(cmd));
}

void DebugDraw::pushCircleXZ(const glm::vec3& center, float radius,
                             const glm::vec3& color, int segments) {
  const float step = glm::two_pi<float>() / static_cast<float>(segments);
  for (int i = 0; i < segments; ++i) {
    float a0 = step * i;
    float a1 = step * (i + 1);
    glm::vec3 p0 = center + glm::vec3(cosf(a0) * radius, 0.0f, sinf(a0) * radius);
    glm::vec3 p1 = center + glm::vec3(cosf(a1) * radius, 0.0f, sinf(a1) * radius);
    pushLine(p0, p1, color);
  }
}

// Arc in the XY plane: theta goes from startRad to endRad
// At theta: point = center + vec3(cos(theta) * radius, sin(theta) * radius, 0)
void DebugDraw::pushArcXY(const glm::vec3& center, float radius,
                          float startRad, float endRad,
                          const glm::vec3& color, int segments) {
  const float step = (endRad - startRad) / static_cast<float>(segments);
  for (int i = 0; i < segments; ++i) {
    float t0 = startRad + step * i;
    float t1 = startRad + step * (i + 1);
    glm::vec3 p0 = center + glm::vec3(cosf(t0) * radius, sinf(t0) * radius, 0.0f);
    glm::vec3 p1 = center + glm::vec3(cosf(t1) * radius, sinf(t1) * radius, 0.0f);
    pushLine(p0, p1, color);
  }
}

// Arc in ZY plane: theta goes from startRad to endRad
// At theta: point = center + vec3(0, sin(theta) * radius, cos(theta) * radius)
void DebugDraw::pushArcZY(const glm::vec3& center, float radius,
                          float startRad, float endRad,
                          const glm::vec3& color, int segments) {
  const float step = (endRad - startRad) / static_cast<float>(segments);
  for (int i = 0; i < segments; ++i) {
    float t0 = startRad + step * i;
    float t1 = startRad + step * (i + 1);
    glm::vec3 p0 = center + glm::vec3(0.0f, sinf(t0) * radius, cosf(t0) * radius);
    glm::vec3 p1 = center + glm::vec3(0.0f, sinf(t1) * radius, cosf(t1) * radius);
    pushLine(p0, p1, color);
  }
}

void DebugDraw::capsule(const glm::vec3& center, float radius, float cylinderHalfHeight, const glm::vec3& color) {
  
  const glm::vec3 topCenter = center + glm::vec3(0.0f, cylinderHalfHeight, 0.0f);
  const glm::vec3 botCenter = center + glm::vec3(0.0f, -cylinderHalfHeight, 0.0f);

  pushCircleXZ(topCenter, radius, color);
  pushCircleXZ(botCenter, radius, color);

  static const float cardinals[] = { 0.0f, glm::half_pi<float>(),
                                    glm::pi<float>(), glm::three_over_two_pi<float>()};
  for (float angle : cardinals) {
    glm::vec3 offset(cosf(angle) * radius, 0.0f, sinf(angle) * radius);
    pushLine(topCenter + offset, botCenter + offset, color);
  }

  pushArcXY(topCenter, radius, 0.0f, glm::pi<float>(), color);
  pushArcZY(topCenter, radius, 0.0f, glm::pi<float>(), color);

  pushArcXY(botCenter, radius, 0.0f, -glm::pi<float>(), color);
  pushArcZY(botCenter, radius, 0.0f, -glm::pi<float>(), color);
}

void DebugDraw::capsulePQ(const glm::vec3& p0,
                          const glm::vec3& p1,
                          float radius,
                          const glm::vec3& color)
{
  constexpr int M = 8;
  constexpr int N = 12;

  glm::vec3 axis = p1 - p0;
  float len = glm::length(axis);
  if (len < 1e-6f) { sphere(p0, radius, color); return; }
  axis /= len;

  glm::vec3 ref = (std::abs(axis.y) < 0.9f) ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
  glm::vec3 u = glm::normalize(glm::cross(ref, axis));
  glm::vec3 v = glm::cross(axis, u);

  // Axis line
  line(p0, p1, color);

  // two end rings + connector lines
  std::vector<glm::vec3> ring0(N), ring1(N);
  for (int i = 0; i < N; ++i) {
    float a = glm::two_pi<float>() * i / N;
    glm::vec3 offset = radius * (std::cos(a) * u + std::sin(a) * v);
    ring0[i] = p0 + offset;
    ring1[i] = p1 + offset;
  }
  for (int i = 0; i < N; ++i) {
    line(ring0[i], ring0[(i + 1) % N], color);
    line(ring1[i], ring1[(i + 1) % N], color);
    line(ring0[i], ring1[i], color);
  }

  auto pushHemisphere = [&](const glm::vec3& center, const glm::vec3& poleDir) {
    for (int plane = 0; plane < 2; ++plane) {
      const glm::vec3& tangent = (plane == 0) ? u : v;
      glm::vec3 prev = center + radius * tangent;
      for (int j = 0; j <= M; ++j) {
        float t = glm::pi<float>() * j / M;
        glm::vec3 curr = center + radius * (std::cos(t) * tangent
                                            + std::sin(t) * poleDir);
        line(prev, curr, color);
        prev = curr;
      }
    }
  };
  pushHemisphere(p1, axis);
  pushHemisphere(p0, -axis);
}

void DebugDraw::box(const glm::vec3& center,
                    const glm::vec3& halfExtents,
                    const glm::vec3& color)
  {
  const float x = halfExtents.x, y = halfExtents.y, z = halfExtents.z;
  glm::vec3 c[8] = {
    center + glm::vec3(-x, -y, -z), center + glm::vec3( x, -y, -z),
    center + glm::vec3( x, -y,  z), center + glm::vec3(-x, -y,  z),
    center + glm::vec3(-x,  y, -z), center + glm::vec3( x,  y, -z),
    center + glm::vec3( x,  y,  z), center + glm::vec3(-x,  y,  z),
  };

  // Bottom face
  pushLine(c[0], c[1], color); pushLine(c[1], c[2], color);
  pushLine(c[2], c[3], color); pushLine(c[3], c[0], color);

  // top face
  pushLine(c[4], c[5], color); pushLine(c[5], c[6], color);
  pushLine(c[6], c[7], color); pushLine(c[7], c[4], color);

  // vertical edges
  pushLine(c[0], c[4], color); pushLine(c[1], c[5], color);
  pushLine(c[2], c[6], color); pushLine(c[3], c[7], color);
}

void DebugDraw::line(const glm::vec3& a, const glm::vec3& b, const glm::vec3& color) {
  pushLine(a, b, color);
}

void DebugDraw::boxOBB(const glm::mat4& t, const glm::vec3& h, const glm::vec3& col) {
  glm::vec3 ax = glm::normalize(glm::vec3(t[0])) * h.x; 
  glm::vec3 ay = glm::normalize(glm::vec3(t[1])) * h.y; 
  glm::vec3 az = glm::normalize(glm::vec3(t[2])) * h.z; 
  glm::vec3 c = glm::vec3(t[3]); 

  // Enumerate 8 corners using sign combinations of the three axes
  glm::vec3 v[8] = {
    c + ax + ay + az, // 0: +++
    c + ax + ay - az, // 1: ++-
    c + ax - ay + az, // 2: +-+
    c + ax - ay - az, // 3: +--
    c - ax + ay + az, // 4: -++
    c - ax + ay - az, // 5: -+-
    c - ax - ay + az, // 6: --+
    c - ax - ay - az, // 7: ---
  };

  pushLine(v[0], v[1], col); pushLine(v[2], v[3], col);
  pushLine(v[4], v[5], col); pushLine(v[6], v[7], col);
  // 4 along Y (top/bottom pairs)
  pushLine(v[0], v[2], col); pushLine(v[1], v[3], col);
  pushLine(v[4], v[6], col); pushLine(v[5], v[7], col);
  // 4 along X (left/right pairs)
  pushLine(v[0], v[4], col); pushLine(v[1], v[5], col);
  pushLine(v[2], v[6], col); pushLine(v[3], v[7], col);
}

void DebugDraw::sphere(const glm::vec3& c, float r, const glm::vec3& col, int seg) {
  pushCircleXZ(c, r, col, seg);
  pushArcXY(c, r, 0.0f, glm::two_pi<float>(), col, seg);
  pushArcZY(c, r, 0.0f, glm::two_pi<float>(), col, seg);
}

void DebugDraw::axes(const glm::mat4& t, float size) {
  glm::vec3 origin = glm::vec3(t[3]);
  pushLine(origin, origin + glm::normalize(glm::vec3(t[0])) * size, { 1, 0, 0}); // X red
  pushLine(origin, origin + glm::normalize(glm::vec3(t[1])) * size, { 0, 1, 0}); // Y red
  pushLine(origin, origin + glm::normalize(glm::vec3(t[2])) * size, { 0, 0, 1}); // X red
}

void DebugDraw::flush(const glm::mat4& viewProjection) {
  if (!s_initialized || !s_shader || s_commands.empty()) return;

  GLboolean depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
  GLint prevDepthFunc;
  glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
  GLint prevLineWidth = 1;

  // Draw ontop of depth buffer using LEQUAL so lines sitting
  // exactly on a surface (e.g. a capsule equator ring on the floor) dont z-fight away.
  // Keep depth test ON so capsules occluded by walls are clipped,
  // maintaining spatial context rather than a psychedllic funk of outlines
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glLineWidth(1.5f);

  s_shader->bind();
  s_shader->setMat4("u_vp", viewProjection);

  glBindVertexArray(s_vao);
  glBindBuffer(GL_ARRAY_BUFFER, s_vbo);

  for (const auto& cmd : s_commands) {
    if (cmd.vertices.empty()) continue;

    // Upload this commands vertex data
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(cmd.vertices.size() * sizeof(Vertex)),
                 cmd.vertices.data(),
                 GL_DYNAMIC_DRAW);

    s_shader->setVec3("u_color", cmd.color);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(cmd.vertices.size()));
  }

  glBindVertexArray(0);
  s_shader->unbind();

  // Restore depth state
  glDepthFunc(prevDepthFunc);
  if (!depthTestWasEnabled) glDisable(GL_DEPTH_TEST);
  glLineWidth(1.0f);

  s_commands.clear();
}
}
