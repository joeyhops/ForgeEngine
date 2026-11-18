#include "support/TempDir.h"

#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace forge::test {

TempDir::TempDir() {
  static int s_counter = 0;
  std::random_device rd;

  const auto base = std::filesystem::temp_directory_path() / "forge_tests";
  std::filesystem::create_directories(base);

  for (int attempt = 0; attempt < 100; ++attempt) {
    const auto candidate = base / ("t" + std::to_string(rd()) + "_" + std::to_string(s_counter++));
    if (std::filesystem::create_directory(candidate)) {
      m_path = candidate;
      return;
    }
  }
  throw std::runtime_error("TempDir: could not create a unique directory");
}

TempDir::~TempDir() {
  std::error_code ec;
  std::filesystem::remove_all(m_path, ec);
}

std::string TempDir::write(const std::string &name, std::string_view contents) const {
  const auto p = m_path / name;
  std::filesystem::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary);
  f.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  return p.string();
}

std::string TempDir::read(const std::string& name) const {
  std::ifstream f(m_path / name, std::ios::binary);
  if (!f) return {};
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

}
