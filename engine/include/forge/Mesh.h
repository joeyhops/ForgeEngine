#pragma once
#include <vector>

namespace forge {

struct Vertex {
  float position[3];
  float color[3];
};

class Mesh {
public:
  Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices);
  ~Mesh();

  // non-copyable (owns GPU resources)
  Mesh(const Mesh&) = delete;
  Mesh& operator=(const Mesh&) = delete;

  void draw() const;

private:
  void setupGPU(const std::vector<Vertex>& vertices,
                const std::vector<unsigned int>& indices);

  unsigned int m_vao = 0;
  unsigned int m_vbo = 0;
  unsigned int m_ebo = 0;
  unsigned int m_indexCount = 0;

};
}
