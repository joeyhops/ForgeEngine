#include "support/TestPaths.h"

#include <cstdlib>
#include <fstream>
#include <string_view>

namespace forge::test {

bool blessMode() {
  const char* v = std::getenv("FORGE_TEST_BLESS");
  return v != nullptr && *v != '\0' && std::string_view(v) != "0";
}

bool isLfsPointer(const std::filesystem::path& p) {
  constexpr std::string_view kMagic = "version https://git-lfs.github.com/spec/v1";

  std::ifstream f(p, std::ios::binary);
  if (!f) return false;

  std::string head(kMagic.size(), '\0');
  f.read(head.data(), static_cast<std::streamsize>(head.size()));
  return f.gcount() == static_cast<std::streamsize>(kMagic.size()) && head == kMagic;
}

}
