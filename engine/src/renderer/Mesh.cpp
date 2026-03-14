#include <forge/Mesh.h>
#include <GL/glew.h>
#include <vector>

namespace forge {

Mesh::Mesh(const std::vector<Vertex>& vertices,
           const std::vector<unsigned int>& indices)
{
  m_indexCount = static_cast<unsigned int>(indices.size());
  setupGPU(vertices, indices);
}

Mesh::~Mesh() {
  glDeleteVertexArrays(1, &m_vao);
  glDeleteBuffers(1, &m_vbo);
  glDeleteBuffers(1, &m_ebo);
}

void Mesh::setupGPU(const std::vector<Vertex>& vertices,
                    const std::vector<unsigned int>& indices)
{
  glGenVertexArrays(1, &m_vao);
  glBindVertexArray(m_vao);

  // Upload vertex data
  glGenBuffers(1, &m_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
  glBufferData(GL_ARRAY_BUFFER,
               vertices.size() * sizeof(Vertex),
               vertices.data(),
               GL_STATIC_DRAW);

  // Upload index data
  glGenBuffers(1, &m_ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               indices.size() * sizeof(unsigned int),
               indices.data(),
               GL_STATIC_DRAW);

  // Position attribute - location 0, 3 floats, at byte offset 0
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                        sizeof(Vertex), (void*)offsetof(Vertex, position));
  glEnableVertexAttribArray(0);

  // Color attribute - location 1, 3 floats, offset past the position floats
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                        sizeof(Vertex), (void*)offsetof(Vertex, color));
  glEnableVertexAttribArray(1);

  glBindVertexArray(0);
}

void Mesh::draw() const {
  glBindVertexArray(m_vao);
  glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
}
}
