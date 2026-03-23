set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_library(forge_engine STATIC
  src/core/Application.cpp
  src/core/Logger.cpp
  src/renderer/Shader.cpp
  src/renderer/Mesh.cpp
  src/renderer/Camera.cpp
  src/renderer/Animator.cpp
  src/scene/Transform.cpp
  src/scripting/LuaState.cpp
  src/physics/PhysicsWorld.cpp
  src/physics/RigidBodyComponent.cpp
  src/combat/CombatComponent.cpp
  src/combat/CombatSystem.cpp
  src/events/EventBus.cpp
  src/events/FlagManager.cpp
  src/ai/AIComponent.cpp
)

target_include_directories(forge_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LUA_INCLUDE_DIR}
    ${BULLET_INCLUDE_DIRS}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

find_package(glfw3 CONFIG REQUIRED)
find_package(GLEW CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Lua REQUIRED)
find_package(sol2 CONFIG REQUIRED)
find_package(Bullet CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

find_package(spdlog CONFIG REQUIRED)

target_link_libraries(forge_engine PUBLIC
  glfw 
  opengl32
  GLEW::GLEW
  glm::glm
  ${LUA_LIBRARIES}
  sol2::sol2
  ${BULLET_LIBRARIES}
  nlohmann_json::nlohmann_json
  spdlog::spdlog 
)
