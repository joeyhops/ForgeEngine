#include "support/LogCapture.h"

#include <spdlog/common.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <memory>
#include <mutex>

namespace forge::test {
namespace {

class CaptureSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
  std::vector<CapturedLine> snapshot() {
    std::lock_guard<std::mutex> lock(this->mutex_);
    return m_lines;
  }
  void reset() {
    std::lock_guard<std::mutex> lock(this->mutex_);
    m_lines.clear();
  }

protected:
  void sink_it_(const spdlog::details::log_msg& msg) override {
    m_lines.push_back({ msg.level, std::string(msg.payload.data(), msg.payload.size()) });
  }
  void flush_() override {}

private:
  std::vector<CapturedLine> m_lines;
};

std::shared_ptr<CaptureSink>& captureSink() {
  static std::shared_ptr<CaptureSink> s_sink;
  return s_sink;
}

}

void LogCapture::install(bool echoToConsole) {
  captureSink() = std::make_shared<CaptureSink>();

  std::vector<spdlog::sink_ptr> sinks{ captureSink() };
  if (echoToConsole)
    sinks.push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());

  auto logger = std::make_shared<spdlog::logger>("forge-test", sinks.begin(), sinks.end());
  logger->set_level(spdlog::level::trace);
  logger->set_pattern("[%l] %v");
  spdlog::set_default_logger(logger);
}

void LogCapture::clear() {
  if (captureSink()) captureSink()->reset();
}

std::vector<CapturedLine> LogCapture::lines() {
  return captureSink() ? captureSink()->snapshot() : std::vector<CapturedLine>{};
}

bool LogCapture::contains(spdlog::level::level_enum level, std::string_view needle) {
  for (const auto& l : lines())
    if (l.level == level && l.text.find(needle) != std::string::npos) return true;
  return false;
}

int LogCapture::count(spdlog::level::level_enum level, std::string_view needle) {
  int n = 0;
  for (const auto& l : lines())
    if (l.level == level && (needle.empty() || l.text.find(needle) != std::string::npos)) ++n;
  return n;
}

std::string LogCapture::dump() {
  std::string out;
  for (const auto& l : lines()) {
    const auto name = spdlog::level::to_string_view(l.level);
    out.append("[").append(name.data(), name.size()).append("] ").append(l.text).append("\n");
  }
  return out;
}

}
