#include "support/Golden.h"
#include "support/TestPaths.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace forge::test {
namespace {

std::string readAll(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

void writeAll(const std::filesystem::path& p, const std::string& s) {
  std::filesystem::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary);
  f << s;
}

std::vector<std::string> splitLines(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream ss(s);
  std::string line;
  while (std::getline(ss, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    out.push_back(line);
  }
  return out;
}

}

void checkGolden(const std::string& name, const std::string& actual) {
  const auto goldenPath = goldenDir() / name;

  if (blessMode()) {
    writeAll(goldenPath, actual);
    WARN("blessed golden: " << goldenPath.string() << " - review `git diff` before committing");
    return;
  }

  if (!std::filesystem::exists(goldenPath)) {
    FAIL("missing golden '" << goldenPath.string() << "'. Run once with FORGE_TEST_BLESS=1, then READ the file before committing it");
  }

  const auto expectedLines = splitLines(readAll(goldenPath));
  const auto actualLines = splitLines(actual);
  if (expectedLines == actualLines) {
    SUCCEED();
    return;
  }

  const auto actualPath = binaryDir() / "golden_actual" / name;
  writeAll(actualPath, actual);

  const size_t n = std::min(expectedLines.size(), actualLines.size());
  size_t first = n;
  for (size_t i = 0; i < n; ++i)
    if (expectedLines[i] != actualLines[i]) { first = i; break; }

  const std::string exp = first < expectedLines.size() ? expectedLines[first] : "<end of file>";
  const std::string act = first < actualLines.size() ? actualLines[first] : "<end of file>";

  FAIL("golden mismatch '" << name << "' at line " << (first + 1) << "\n"
       << " expected: " << exp << "\n"
       << " actual: " << act << "\n"
       << " full actual output: " << actualPath.string() << "\n"
       << " diff:    diff " << goldenPath.string() << " " << actualPath.string() << "\n"
       << " If the change is intended, re-run with FORGE_TEST_BLESS=1 and review the git diff.");
}

}
