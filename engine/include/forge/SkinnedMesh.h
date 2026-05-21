#pragma once
#include <forge/SkinnedVertex.h>
#include <vector>

namespace forge {

// GPU Mesh that carries bone influence data alongside the
// standard position/normal/texCoord attribs
//
// VAO Layout:
//   location 0 position float[3]
//   location 1 normal float[3]
//   location 2 texCoord float[2]
//   location 3 boneIds int[4] -- glVertexAttribIPointer (int, not float)
//   location 4 boneWeights float[4]
//   location 5 tangent float[4] -- xyz = tangent vector, w = handedness sign
//
//   Draw() is identical to Mesh so the rest of the engine can treat both
//   the same way

class SkinnedMesh {
public:
  SkinnedMesh(const std::vector<SkinnedVertex>& vertices,
              const std::vector<unsigned int>& indices);
  ~SkinnedMesh();

  SkinnedMesh(const SkinnedMesh&) = delete;
  SkinnedMesh& operator=(const SkinnedMesh&) = delete;

  void draw() const;

private:
  void setupGPU(const std::vector<SkinnedVertex>& vertices,
                const std::vector<unsigned int>& indices);
  
  unsigned int m_vao = 0;
  unsigned int m_vbo = 0;
  unsigned int m_ebo = 0;
  unsigned int m_indexCount = 0;
};

}
