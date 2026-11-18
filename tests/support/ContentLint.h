#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace forge::test {

// Problem keys are "<asset path>:<location>:<code>". They're compared against
// committed baselines, so they must stay deterministic (sorted inputs,
// index-based locations, no absolute paths)

std::vector<std::string> findAssetFiles(const std::string& subdir, const std::string& suffix,
                                        bool recursive);

std::string assetRelative(const std::filesystem::path& abs);

bool assetExists(const std::string& rel);

std::vector<std::string> lintGraph(const std::string& rel);
std::vector<std::string> lintTae(const std::string& rel);
std::vector<std::string> lintDefs();

}
