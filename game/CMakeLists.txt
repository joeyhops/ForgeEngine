add_executable(game
  src/main.cpp
  src/stb_impl.cpp
  src/ForgeGame.cpp
  src/PlayerEntity.cpp
  src/EnemyEntity.cpp
  src/CharacterGraph.cpp
  src/InputManager.cpp
)

target_link_libraries(game PRIVATE forge_engine)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            PROPERTY VS_STARTUP_PROJECT game)
if (UNIX)
  set_target_properties(game PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
  )
endif()

