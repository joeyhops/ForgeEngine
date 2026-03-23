#include "ForgeGame.h"
#include <forge/Logger.h>
#include <iostream>

int main() {
  forge::Logger::init();
  try {
    ForgeGame game;
    game.run();
  }
  catch (const std::exception& e) {
    LOG_CRITICAL("Fatal: {}", e.what());
    return -1;
  }

  return 0;
}
