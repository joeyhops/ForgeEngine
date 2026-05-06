add_executable(tae_editor
  src/main.cpp
  src/TaeEditorApp.cpp
  src/TaeEditorUI.cpp
)

target_link_libraries(tae_editor PRIVATE forge_engine)

if (UNIX)
  set_target_properties(tae_editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
  )
endif()
