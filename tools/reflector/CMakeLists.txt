cmake_minimum_required(VERSION 3.20)
project(forge-reflect LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# libclang
# prefer LLVM CMake config package. Fall back to manual path search for distros
# that ship only the bare .so also derive clang resource dir (builtin headers)
# matching the LINKED libclang version
find_package(LLVM CONFIG QUIET)
if (LLVM_FOUND)
  message(STATUS "forge-reflect: LLVM ${LLVM_VERSION} via CMake config")
  find_library(LIBCLANG_LIB
    NAMES clang libclang
    HINTS ${LLVM_LIBRARY_DIRS}
    REQUIRED
  )
  set(LIBCLANG_INCLUDE_DIRS ${LLVM_INCLUDE_DIRS})
  set(FORGE_CLANG_RESOURCE_DIR "${LLVM_LIBRARY_DIRS}/clang/${LLVM_VERSION_MAJOR}")
else()
  # fallback path search
  #  # Common system locations on Ubuntu/Arch for LLVM 16-18
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
      /usr/lib
      /usr/lib/x86_64-linux-gnu
    REQUIRED
  )
  message(STATUS "forge-reflect: libclang at ${LIBCLANG_LIB}")
  # Ask a clang binary for the matching resource dir
  find_program(CLANG_BIN NAMES clang clang-18 clang-17 clang-16)
  if (CLANG_BIN)
    execute_process(
      COMMAND "${CLANG_BIN}" -print-resource-dir
      OUTPUT_VARIABLE FORGE_CLANG_RESOURCE_DIR
      OUTPUT_STRIP_TRAILING_WHITESPACE)
  endif()
endif()


if (APPLE)
  execute_process(
    COMMAND xcrun --show-sdk-path
    OUTPUT_VARIABLE FORGE_MACOS_SYSROOT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _xcrun_rc)

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

if (FORGE_CLANG_RESOURCE_DIR)
  message(STATUS "forge-reflect: clang resource dir = ${FORGE_CLANG_RESOURCE_DIR}")
  target_compile_definitions(forge-reflect PRIVATE
    FORGE_CLANG_RESOURCE_DIR="${FORGE_CLANG_RESOURCE_DIR}")
endif()

if (APPLE AND _xcrun_rc EQUAL 0 AND FORGE_MACOS_SYSROOT)
  message(STATUS "forge-reflect: macOS sysroot = ${FORGE_MACOS_SYSROOT}")
  target_compile_definitions(forge-reflect PRIVATE
    FORGE_MACOS_SYSROOT="${FORGE_MACOS_SYSROOT}")
endif()

# libc++ headers that match the linked libclang (homebrew)
# LLVM_INCLUDE_DIRS is e.g. /opt/homebrew/llvm/include
if (APPLE AND LLVM_FOUND AND EXISTS "${LLVM_INCLUDE_DIRS}/c++/v1")
  target_compile_definitions(forge-reflect PRIVATE
    FORGE_LIBCXX_INCLUDE="${LLVM_INCLUDE_DIRS}/c++/v1")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" AND
  CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
  target_link_libraries(forge-reflect PRIVATE stdc++fs)
endif()
