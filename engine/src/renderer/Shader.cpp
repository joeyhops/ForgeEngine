#include <forge/Shader.h>
#include <forge/Logger.h>

#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace forge {

Shader::Shader(const std::string& vertPath, const std::string& fragPath) {
  std::string vertSrc = readFile(vertPath);
  std::string fragSrc = readFile(fragPath);

  unsigned int vert = compileShader(GL_VERTEX_SHADER, vertSrc);
  unsigned int frag = compileShader(GL_FRAGMENT_SHADER, fragSrc);

  // link shaders into single GPU program
  m_programID = glCreateProgram();
  glAttachShader(m_programID, vert);
  glAttachShader(m_programID, frag);
  glLinkProgram(m_programID);

  // err check
  int success;
  glGetProgramiv(m_programID, GL_LINK_STATUS, &success);
  if (!success) {
    char log[512];
    glGetProgramInfoLog(m_programID, 512, nullptr, log);
    throw std::runtime_error(std::string("Shader link failed:\n") + log);
  }

  glDeleteShader(vert);
  glDeleteShader(frag);

  LOG_INFO("Shader loaded: {} + {}", vertPath, fragPath);
}

Shader::~Shader() {
  glDeleteProgram(m_programID);
}

void Shader::bind() const { glUseProgram(m_programID); }
void Shader::unbind() const { glUseProgram(0); }

int Shader::getUniformLocation(const std::string& name) const {
  int loc = glGetUniformLocation(m_programID, name.c_str());
  if (loc == -1)
    LOG_WARN("Uniform '{}' not found in shader", name);
  return loc;
}

void Shader::setMat4(const std::string& name, const glm::mat4& mat) const {
  glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, glm::value_ptr(mat));
}

void Shader::setVec3(const std::string& name, const glm::vec3& vec) const {
  glUniform3fv(getUniformLocation(name), 1, glm::value_ptr(vec));
}

void Shader::setFloat(const std::string& name, float value) const {
  glUniform1f(getUniformLocation(name), value);
}

void Shader::setInt(const std::string& name, int value) const {
  glUniform1i(getUniformLocation(name), value);
}

std::string Shader::readFile(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open())
    throw std::runtime_error("Cannot open shader file: " + path);
  std::stringstream ss;
  ss << file.rdbuf();
  return ss.str();
}

unsigned int Shader::compileShader(unsigned int type, const std::string& source) {
  unsigned int id = glCreateShader(type);
  const char* src = source.c_str();
  glShaderSource(id, 1, &src, nullptr);
  glCompileShader(id);

  int success;
  glGetShaderiv(id, GL_COMPILE_STATUS, &success);
  if (!success) {
    char log[512];
    glGetShaderInfoLog(id, 512, nullptr, log);
    const char* typeName = (type == GL_VERTEX_SHADER) ? "vertex" : "fragment";
    throw std::runtime_error(std::string(typeName) + " shader compile failed:\n" + log);
  }
  return id;
}
}
