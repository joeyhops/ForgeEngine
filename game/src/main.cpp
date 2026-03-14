#include <forge/Application.h>
#include <forge/Logger.h>
#include <iostream>

int main() {
  forge::Logger::init();
  try {
    forge::Application app(1280, 720, "Forge Engine v0.1");
    app.run();
  }
  catch (const std::exception& e) {
    LOG_CRITICAL("Unhandled exception: {}", e.what());
    return -1;
  }

  return 0;
}
