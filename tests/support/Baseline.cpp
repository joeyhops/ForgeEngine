#include "support/Baseline.h"
#include "support/TestPaths.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>

namespace forge::test {
namespace {

std::string trim(std::string s) {
  while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
  return s.substr(i);
}

}

void checkBaseline(const std::string& name, const std::vector<std::string>& problems) {
  const std::set<std::string> found(problems.begin(), problems.end());
  const auto path = baselineDir() / name;

  if (blessMode()) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << "# Content baseline for " << name << "\n"
        << "# Each line is a KNOWN content problem accepted for now - i.e. a TODO.\n"
        << "# The test fails if a new problem appears OR a listed one disappears.\n"
        << "# Regenerate with FORGE_TEST_BLESS=1, then review git diff.\n";
    for (const auto& p : found) out << p << '\n';
    WARN("blessed baseline " << path.string() << " (" << found.size() << " entries)");
    return;
  }

  std::set<std::string> known;
  if (std::ifstream in(path); in) {
    std::string line;
    while (std::getline(in, line)) {
      line = trim(line);
      if (line.empty() || line[0] == '#') continue;
      known.insert(line);
    }
  } else if (!found.empty()) {
    FAIL("no baseline file " << path.string() << " but " << found.size()
         << " problem(s) found. Run once with FORGE_TEST_BLESS=1 and review result.");
  }

  std::vector<std::string> unexpected, stale;
  std::set_difference(found.begin(), found.end(), known.begin(), known.end(),
                      std::back_inserter(unexpected));
  std::set_difference(known.begin(), known.end(), found.begin(), found.end(),
                      std::back_inserter(stale));

  if (unexpected.empty() && stale.empty()) {
    SUCCEED();
    return;
  }

  std::ostringstream msg;
  if (!unexpected.empty()) {
    msg << unexpected.size() << " NEW content issue(s) - fix the content or add the line to "
        << name << " deliberately:\n";
    for (const auto& p : unexpected) msg << "  + " << p << "\n";
  }
  if (!stale.empty()) {
    msg << stale.size() << " baseline entr" << (stale.size() == 1 ? "y" : "ies")
        << " no longer occur - you fixed something! Delete from " << name << ":\n";
    for (const auto& p : stale) msg << "  - " << p << "\n";
  }
  FAIL(msg.str());
}

}
