#include <forge/Shader.h>
#include <forge/Logger.h>

#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>

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
  auto it = m_uniformCache.find(name);
  if (it != m_uniformCache.end()) return it->second;
  
  int loc = glGetUniformLocation(m_programID, name.c_str());
  if (loc == -1)
    LOG_TRACE("Uniform '{}' not found in shader (Program: {})", name, m_programID);
  m_uniformCache[name] = loc;
  return loc;
}

void Shader::setMat4Array(const std::string& name, int count, const glm::mat4* data) const
{
  int loc = getUniformLocation(name);
  if (loc == -1) return;
  glUniformMatrix4fv(loc, count, GL_FALSE, glm::value_ptr(data[0]));
}

void Shader::setMat4(const std::string& name, const glm::mat4& mat) const {
  glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, glm::value_ptr(mat));
}

void Shader::setVec4(const std::string& name, const glm::vec4& v) const {
  glUniform4f(getUniformLocation(name), v.x, v.y, v.z, v.w);
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

void Shader::setBool(const std::string& name, bool value) const {
  glUniform1i(getUniformLocation(name), (int)value);
}

void Shader::setUniformBlockBinding(const std::string& blockName, int bindingPoint) const {
  unsigned int blockIndex = glGetUniformBlockIndex(m_programID, blockName.c_str());
  if (blockIndex != GL_INVALID_INDEX)
    glUniformBlockBinding(m_programID, blockIndex, bindingPoint);
  else
    LOG_WARN("Uniform block '{}' not found in shader (Program: {})", blockName, m_programID);
}

std::string Shader::readFile(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open())
    throw std::runtime_error("Cannot open shader file: " + path);
  // Extract the directory of this file so we can resolve relative #include paths.
  std::string dir;
  auto sep = path.rfind('/');
  if (sep != std::string::npos)
    dir = path.substr(0, sep + 1);

  std::string result;
  std::string line;
  while (std::getline(file, line)) {
    if (line.substr(0, 8) == "#include") {
      auto q1 = line.find('"');
      auto q2 = (q1 != std::string::npos) ? line.find('"', q1 + 1) : std::string::npos;
      if (q1 != std::string::npos && q2 != std::string::npos) {
        std::string includePath = line.substr(q1 + 1, q2 - q1 - 1);
        result += readFile(dir + includePath);
        result += '\n';
        continue;
      }
    }
    result += line + '\n';
  }
  return result;
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
