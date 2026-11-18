#pragma once

#include <spdlog/common.h>

#include <string>
#include <string_view>
#include <vector>

namespace forge::test {

struct CapturedLine {
  spdlog::level::level_enum level;
  std::string text;
};

class LogCapture {
public:
  static void install(bool echoToConsole);

  static void clear();
  static std::vector<CapturedLine> lines();

  static bool contains(spdlog::level::level_enum level, std::string_view needle);
  static int count(spdlog::level::level_enum level, std::string_view needle = {});

  static std::string dump();
};

}
