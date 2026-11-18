#pragma once

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

namespace forge::test {

inline std::filesystem::path sourceDir() { return FORGE_TEST_SOURCE_DIR; }
inline std::filesystem::path assetDir() { return FORGE_TEST_ASSET_DIR; }
inline std::filesystem::path dataDir() { return FORGE_TEST_DATA_DIR; }
inline std::filesystem::path goldenDir() { return FORGE_TEST_GOLDEN_DIR; }
inline std::filesystem::path baselineDir() { return FORGE_TEST_BASELINE_DIR; }
inline std::filesystem::path binaryDir() { return FORGE_TEST_BINARY_DIR; }

inline std::string dataFile(const std::string& rel) { return (dataDir() / rel).string(); }
inline std::string assetFile(const std::string& rel) { return (assetDir() / rel).string(); }

bool blessMode();

bool isLfsPointer(const std::filesystem::path& p);

}

#define FORGE_REQUIRE_REAL_ASSET(relPath) \
  do { \
    const auto forgeAssetPath_ = ::forge::test::assetDir() / (relPath); \
    if (!std::filesystem::exists(forgeAssetPath_)) \
      FAIL("asset does not exist: " << forgeAssetPath_.string()); \
    if (::forge::test::isLfsPointer(forgeAssetPath_)) \
      SKIP("asset is a Git LFS pointer (run `git lfs pull`): " << forgeAssetPath_.string()); \
  } while(false)
