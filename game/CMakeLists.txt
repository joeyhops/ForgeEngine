add_executable(game
  src/main.cpp
  src/ForgeGame.cpp
)

target_link_libraries(game PRIVATE forge_engine)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            PROPERTY VS_STARTUP_PROJECT game)
