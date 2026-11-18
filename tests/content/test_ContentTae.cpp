#include "support/Baseline.h"
#include "support/ContentLint.h"
#include "support/TestPaths.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace forge::test;

TEST_CASE("Content: TAE sidecars are well-formed (against baseline)",
          "[content][tae]") {
  const auto sidecars = findAssetFiles("anim", ".tae.json", true);
  REQUIRE_FALSE(sidecars.empty());

  std::vector<std::string> problems;
  for (const auto& rel : sidecars) {
    const auto p = lintTae(rel);
    problems.insert(problems.end(), p.begin(), p.end());
  }

  // The engine only ever loads <stem>.tae.json, anything else in a tae/ dir is invisible to it
  for (const auto& rel : findAssetFiles("anim", "", true)) {
    const std::filesystem::path p(rel);
    if (p.parent_path().filename() == "tae" && !p.filename().string().ends_with(".tae.json"))
      problems.push_back(rel + ":stray-file-in-tae-dir");
  }
  checkBaseline("tae.txt", problems);
}
