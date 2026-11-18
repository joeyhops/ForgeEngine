#include "support/Baseline.h"
#include "support/ContentLint.h"

#include <catch2/catch_test_macros.hpp>

using namespace forge::test;

TEST_CASE("Content: animation graphs pass structural lint (against baseline)",
          "[content][anim][graph]") {
  const auto graphs = findAssetFiles("anim", ".json", false);
  REQUIRE_FALSE(graphs.empty());

  std::vector<std::string> problems;
  for (const auto& rel : graphs) {
    const auto p = lintGraph(rel);
    problems.insert(problems.end(), p.begin(), p.end());
  }
  checkBaseline("graphs.txt", problems);
}
