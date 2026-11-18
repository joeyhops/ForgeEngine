#pragma once

#include <glm/glm.hpp>

#include <catch2/catch_tostring.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <cmath>
#include <sstream>
#include <string>

namespace Catch {
template <>
struct StringMaker<glm::vec3> {
  static std::string convert(const glm::vec3& v) {
    std::ostringstream ss;
    ss << "vec3(" << v.x << ", " << v.y << ", " << v.z << ")";
    return ss.str();
  }
};
}

namespace forge::test {

class Vec3Near final : public Catch::Matchers::MatcherBase<glm::vec3> {
public:
  Vec3Near(glm::vec3 target, float eps) : m_target(target), m_eps(eps) {}

  bool match(const glm::vec3& v) const override {
    return std::abs(v.x - m_target.x) <= m_eps &&
           std::abs(v.y - m_target.y) <= m_eps &&
           std::abs(v.z - m_target.z) <= m_eps;
  }

  std::string describe() const override {
    return "is within " + std::to_string(m_eps) + " of " +
            Catch::StringMaker<glm::vec3>::convert(m_target);
  }

private:
  glm::vec3 m_target;
  float m_eps;
};

inline Vec3Near Near(glm::vec3 target, float eps = 1e-4f) { return { target, eps }; }

}
