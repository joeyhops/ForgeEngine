#pragma once
#include <string>
#include <glm/glm.hpp>

namespace forge {

class Shader {
public:
  Shader(const std::string& vertPath, const std::string& fragPath);
  ~Shader();

  // non-copyable (owns GPU resource)
  Shader(const Shader&) = delete;
  Shader& operator=(const Shader&) = delete;

  void bind() const; // use this shader for subsequent draws
  void unbind() const;

  // Uniform setters - send data from CPU to GPU shader
  void setMat4(const std::string& name, const glm::mat4& mat) const;
  void setVec3(const std::string& name, const glm::vec3& vec) const;
  void setFloat(const std::string& name, float value) const;
  void setInt(const std::string& name, int value) const;
  void setBool(const std::string& name, bool value) const;

private:
  unsigned int m_programID = 0;

  unsigned int compileShader(unsigned int type, const std::string& source);
  std::string readFile(const std::string& path);
  int getUniformLocation(const std::string& name) const;
};
}
