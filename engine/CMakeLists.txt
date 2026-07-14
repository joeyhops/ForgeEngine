cmake_policy(SET CMP0118 NEW)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)

set (REFLECT_HEADERS
  ${CMAKE_CURRENT_SOURCE_DIR}/include/forge/HitboxTypes.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/forge/AnimationClip.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/forge/MaterialDefinition.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/forge/AnimGraph.h
)

set(GENERATED_SOURCES)
foreach(HDR ${REFLECT_HEADERS})
  get_filename_component(STEM ${HDR} NAME_WE)
  list(APPEND GENERATED_SOURCES ${GENERATED_DIR}/${STEM}.gen.cpp)
endforeach()

add_custom_command(
  OUTPUT ${GENERATED_SOURCES}
  COMMAND $<TARGET_FILE:forge-reflect>
          --compile-db ${CMAKE_BINARY_DIR}/compile_commands.json
          --headers ${REFLECT_HEADERS}
          --output-dir ${GENERATED_DIR}
  DEPENDS ${REFLECT_HEADERS}
  COMMENT "ForgeReflect: generating type registration code"
  VERBATIM
)

#set_source_files_properties(${GENERATED_SOURCES} PROPERTIES GENERATED TIME)

add_library(forge_engine STATIC
  src/core/Application.cpp
  src/core/Logger.cpp
  src/core/LevelLoader.cpp
  src/core/TAESerialization.cpp
  src/core/TypeSystem.cpp
  src/map/GeometryGenerator.cpp
  src/map/MapScene.cpp
  src/map/EntityAssembler.cpp
  src/renderer/Renderer.cpp
  src/renderer/Material.cpp
  src/renderer/LightEnvironment.cpp
  src/renderer/Shader.cpp
  src/renderer/DebugDraw.cpp
  src/renderer/Mesh.cpp
  src/renderer/SkinnedMesh.cpp
  src/renderer/Camera.cpp
  src/renderer/Animator.cpp
  src/renderer/Texture.cpp
  src/renderer/DebugUI.cpp
  src/renderer/AnimGraph.cpp
  src/renderer/ThirdPersonCamera.cpp
  src/renderer/TaskSystem.cpp
  src/renderer/SyncTrack.cpp
  src/renderer/SampledEventsBuffer.cpp
  src/renderer/RootMotionDebugger.cpp
  src/scene/Transform.cpp
  src/scripting/LuaState.cpp
  src/physics/PhysicsWorld.cpp
  src/physics/RigidBodyComponent.cpp
  src/physics/CharacterController.cpp
  src/physics/TriggerVolume.cpp
  src/combat/CombatComponent.cpp
  src/combat/CombatSystem.cpp
  src/combat/EquipmentComponent.cpp
  src/events/EventBus.cpp
  src/events/FlagManager.cpp
  src/ai/AIComponent.cpp
  src/assets/AssetManager.cpp
  ${GENERATED_SOURCES}
)

add_dependencies(forge_engine forge-reflect)

target_include_directories(forge_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LUA_INCLUDE_DIR}
    ${BULLET_INCLUDE_DIRS}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

find_package(glfw3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Lua REQUIRED)
find_package(sol2 CONFIG REQUIRED)
find_package(Bullet CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

find_package(spdlog CONFIG REQUIRED)

option(FORGE_AUDIO_FMOD "Enable FMOD Studio Audio Backend" ON)
if(FORGE_AUDIO_FMOD)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
  find_package(FMOD REQUIRED)
  target_compile_definitions(forge_engine PUBLIC FORGE_HAS_FMOD)
endif()

option(FORGE_DEBUG_OVERLAYS "Enable dev debug overlays (RootMotionDebugger, etc.)" ON)
if(FORGE_DEBUG_OVERLAYS)
  target_compile_definitions(forge_engine PUBLIC FORGE_DEBUG)
endif()

target_link_libraries(forge_engine PUBLIC
  glfw 
  OpenGL::GL
  GLEW::GLEW
  glm::glm
  ${LUA_LIBRARIES}
  sol2::sol2
  ${BULLET_LIBRARIES}
  nlohmann_json::nlohmann_json
  assimp::assimp
  imgui::imgui
  spdlog::spdlog 
  $<$<PLATFORM_ID:Linux>:stdc++fs>
  $<$<BOOL:${FORGE_AUDIO_FMOD}>:FMOD::Core>
  $<$<BOOL:${FORGE_AUDIO_FMOD}>:FMOD::Studio>
)
