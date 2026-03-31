#include <forge/SkinnedMesh.h>
#include <GL/glew.h>

namespace forge {

SkinnedMesh::SkinnedMesh(const std::vector<SkinnedVertex>& vertices,
                         const std::vector<unsigned int>& indices)
{
  m_indexCount = static_cast<unsigned int>(indices.size());
  setupGPU(vertices, indices);
}

SkinnedMesh::~SkinnedMesh() {
  glDeleteVertexArrays(1, &m_vao);
  glDeleteBuffers(1, &m_vbo);
  glDeleteBuffers(1, &m_ebo);
}

void SkinnedMesh::setupGPU(const std::vector<SkinnedVertex>& vertices,
                           const std::vector<unsigned int>& indices)
{
  glGenVertexArrays(1, &m_vao);
  glBindVertexArray(m_vao);

  glGenBuffers(1, &m_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
  glBufferData(GL_ARRAY_BUFFER,
               vertices.size() * sizeof(SkinnedVertex),
               vertices.data(),
               GL_STATIC_DRAW);

  glGenBuffers(1, &m_ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               indices.size() * sizeof (unsigned int),
               indices.data(),
               GL_STATIC_DRAW);

  // Location 0 - position (3 floats)
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex), (void*)offsetof(SkinnedVertex, position));
  glEnableVertexAttribArray(0);

  // Location 1 - normal (3 floats)
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex), (void*)offsetof(SkinnedVertex, normal));
  glEnableVertexAttribArray(1);

  // Location 2 - texCoord (2 floats)
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex), (void*)offsetof(SkinnedVertex, texCoord));
  glEnableVertexAttribArray(2);

  // Location 3 - boneIds (4 ints)
  // MUST use glVertexAttribIPointer - the integer variant otherwise
  // glVertexAttribPointer silently converts to floats and corrupts our indices
  glVertexAttribIPointer(3, 4, GL_INT, sizeof(SkinnedVertex), (void*)offsetof(SkinnedVertex, boneIds));
  glEnableVertexAttribArray(3);

  // Location 4 - boneWeights (4 floats)
  glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex), (void*)offsetof(SkinnedVertex, boneWeights));
  glEnableVertexAttribArray(4);

  glBindVertexArray(0);
}

void SkinnedMesh::draw() const {
  glBindVertexArray(m_vao);
  glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
}

}
