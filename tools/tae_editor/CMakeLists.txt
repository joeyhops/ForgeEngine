add_executable(tae_editor
  src/main.cpp
  src/TaeEditorApp.cpp
  src/TaeEditorUI.cpp
)

target_link_libraries(tae_editor PRIVATE
$<$<PLATFORM_ID:Linux>:-Wl,--whole-archive>
forge_engine
$<$<PLATFORM_ID:Linux>:-Wl,--no-whole-archive>
)

if (UNIX)
  set_target_properties(tae_editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
  )
endif()
