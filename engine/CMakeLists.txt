set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_library(forge_engine STATIC
  src/core/Application.cpp
  src/core/Logger.cpp
  src/core/LevelLoader.cpp
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
)

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
)
