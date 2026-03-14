#include <forge/Logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace forge {

void Logger::init() {
  // Colored console output: TRACE=white, INFO=green, WARN=yellow, ERROR=red
  auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  auto logger = std::make_shared<spdlog::logger>("forge", consoleSink);

  // Pattern: [HH:MM:SS] [LEVEL] message
  logger->set_pattern("[%H:%M:%S] [%^%l%$] %v");
  logger->set_level(spdlog::level::trace);

  spdlog::set_default_logger(logger);
  LOG_INFO("Logger initialized");
}

}
