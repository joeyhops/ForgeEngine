#include "support/LogCapture.h"
#include "support/TestPaths.h"

#include <forge/AssetManager.h>
#include <forge/EventBus.h>

#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

bool envFlag(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && *v != '\0' && std::string_view(v) != "0";
}

class ForgeTestListener final : public Catch::EventListenerBase {
public:
  using Catch::EventListenerBase::EventListenerBase;

  void testRunStarting(Catch::TestRunInfo const&) override {
    forge::test::LogCapture::install(envFlag("FORGE_TEST_ECHO_LOGS"));
    forge::AssetManager::setAssetRoot(forge::test::assetDir().string());
  }

  void testCaseStarting(Catch::TestCaseInfo const&) override {
    forge::EventBus::clear();
    forge::test::LogCapture::clear();
  }

  void testCaseEnded(Catch::TestCaseStats const& stats) override {
    const bool failed = stats.totals.assertions.failed > 0;
    if (failed && !stats.testInfo->okToFail()) {
      std::cerr << "\n---- engine log for failed test '" << stats.testInfo->name << "' ----\n"
                << forge::test::LogCapture::dump()
                << "---- end engine log ----\n";
    }
    forge::EventBus::clear();
  }
};

}

CATCH_REGISTER_LISTENER(ForgeTestListener)
