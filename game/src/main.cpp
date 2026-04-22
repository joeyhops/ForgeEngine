#include "ForgeGame.h"
#include <forge/Logger.h>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
  forge::Logger::init();

  std::string mapToLoad = "bonfire";

  for (int i = 0; i < argc; i++) {
    if (std::string(argv[i]) == "--map" && i + 1 < argc) {
      std::string fullPath = argv[i+1];
      size_t lastSlash = fullPath.find_last_of("/\\");
      size_t extension = fullPath.find_last_of(".");
      if (lastSlash != std::string::npos && extension != std::string::npos) {
        mapToLoad = fullPath.substr(lastSlash + 1, extension - lastSlash - 1);
      } else {
        // Assume we just passed a map name
        mapToLoad = fullPath;
      }
    }
  }
  try {
    ForgeGame game;
    game.setInitialMap(mapToLoad);
    game.run();
  }
  catch (const std::exception& e) {
    LOG_CRITICAL("Fatal: {}", e.what());
    return -1;
  }

  return 0;
}
