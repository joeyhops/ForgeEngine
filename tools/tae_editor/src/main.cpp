#include "TaeEditorApp.h"
#include <forge/Logger.h>

int main(int argc, char** argv) {
  forge::Logger::init();
  TaeEditorApp app(1600, 900, "ForgeEngine - TAE Editor");
  if (argc == 3)
    app.setStartupPaths(argv[1], argv[2]);
  app.run();
  return 0;
}
