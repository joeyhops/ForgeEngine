#include <forge/RootMotionPayload.h>

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>

namespace forge {
namespace {

std::string_view trim(std::string_view s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) return {};
  const auto last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

}

bool ParseFloatStrict(std::string_view token, float& out) {
  token = trim(token);
  if (token.empty()) return false;

  // strtof needs a null terminated string, string_view does not guarantee one
  const std::string buff(token);
  char* end = nullptr;
  errno = 0;
  const float v = std::strtof(buff.c_str(), &end);

  if (end != buff.c_str() + buff.size()) return false;
  if (errno == ERANGE || !std::isfinite(v)) return false;

  out = v;
  return true;
}

bool ParseRootMotionPayload(std::string_view payload, RootMotionParams& out) {
  out = RootMotionParams{};
  bool ok = true;

  int index = 0;
  size_t start = 0;
  while (start <= payload.size()) {
    size_t semi = payload.find(';', start);
    if (semi == std::string_view::npos) semi = payload.size();
    const std::string_view token = trim(payload.substr(start, semi - start));

    switch (index) {
      case 0: {
        float s = 0.0f;
        if (!token.empty()) {
          if (ParseFloatStrict(token, s)) out.scale = s;
          else ok = false;
        }
        break;
      }
      case 1: {
        if (!token.empty()) {
          glm::bvec3 axes(false);
          bool axesOk = true;
          for (char c : token) {
            if (c == 'x') axes.x = true;
            else if (c == 'y') axes.y = true;
            else if (c == 'z') axes.z = true;
            else axesOk = false;
          }
          if (axesOk) out.axes = axes;
          else ok = false;
        }
        break;
      }
      case 2: {
        float v = 0.0f;
        if (!token.empty()) {
          if (ParseFloatStrict(token, v) && v > 0.0f) out.maxVelocity = v;
          else ok = false;
        }
        break;
      }
      default:
        ok = false;
        break;
    }

    ++index;
    start = semi + 1;
  }
  return ok;
}

}
