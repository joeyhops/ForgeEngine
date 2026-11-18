add_executable(tae_editor
  src/main.cpp
  src/TaeEditorApp.cpp
  src/TaeEditorUI.cpp
)

forge_link_engine(tae_editor)
forge_enable_warnings(tae_editor)

if (UNIX)
  set_target_properties(tae_editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
  )
endif()
