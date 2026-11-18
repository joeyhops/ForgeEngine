#include "support/Baseline.h"
#include "support/ContentLint.h"

#include <catch2/catch_test_macros.hpp>

using namespace forge::test;

TEST_CASE("Content: weapon and moveset definitions resolve (against baseline)",
          "[content][defs]") {
  REQUIRE_FALSE(findAssetFiles("data/movesets", ".json", false).empty());
  checkBaseline("defs.txt", lintDefs());
}
