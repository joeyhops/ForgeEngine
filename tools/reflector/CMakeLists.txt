cmake_minimum_required(VERSION 3.20)
project(forge-reflect LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# libclang
# prefer LLVM CMake config package. Fall back to manual path search for distros
# that ship only the bare .so
find_package(LLVM CONFIG QUIET)
if (LLVM_FOUND)
  message(STATUS "forge-reflect: LLVM ${LLVM_VERSION} via CMake config")
  find_library(LIBCLANG_LIB
    NAMES clang libclang
    HINTS ${LLVM_LIBRARY_DIRS}
    REQUIRED
  )
  set(LIBCLANG_INCLUDE_DIRS ${LLVM_INCLUDE_DIRS})
else()
  # Common system locations on Ubuntu/Arch for LLVM 16-18
  find_path(LIBCLANG_INCLUDE_DIRS clang-c/Index.h
    HINTS
      /usr/lib/llvm-18/include
      /usr/lib/llvm-17/include
      /usr/lib/llvm-16/include
      /usr/include
    REQUIRED
  )
  find_library(LIBCLANG_LIB
    NAMES clang libclang
    HINTS
      /usr/lib/llvm-18/include
      /usr/lib/llvm-17/include
      /usr/lib/llvm-16/include
      /usr/include
    REQUIRED
  )
  message(STATUS "forge-reflect: libclang at ${LIBCLANG_LIB}")
endif()

find_package(nlohmann_json CONFIG QUIET)
if (nlohmann_json_FOUND)
  message(STATUS "forge-reflect: nlohmann_json via CMake config.")
  set(JSON_TARGET nlohmann_json::nlohmann_json)
else()
  find_path(NLOHMANN_JSON_INCLUDE nlohmann/json.hpp REQUIRED)
  message(STATUS "forge-reflect: nlohmann/json.hpp at ${NLOHMANN_JSON_INCLUDE}")
  add_library(nlohmann_json_iface INTERFACE)
  target_include_directories(nlohmann_json_iface INTERFACE ${NLOHMANN_JSON_INCLUDE})
  set(JSON_TARGET nlohmann_json_iface)
endif()

add_executable(forge-reflect src/main.cpp)

target_include_directories(forge-reflect PRIVATE ${LIBCLANG_INCLUDE_DIRS})

target_link_libraries(forge-reflect PRIVATE
  ${LIBCLANG_LIB}
  ${JSON_TARGET}
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" AND
  CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
  target_link_libraries(forge-reflect PRIVATE stdc++fs)
endif()
