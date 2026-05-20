# Locates FMOD Engine SDK (Core + Studio API Files)
# FMOD Must be downloaded manually and placed at
#   <repo_root>/third_party/fmod/
#
# Alternatively set FMOD_ROOT for custom path
# 
# Defines targets on success:
#   FMOD::Core - fmod / fmodL
#   FMOD::Studio - fmodstudio / fmodstudioL
#
# Sets:
#   FMOD_FOUND
#   FMOD_INCLUDE_DIRS
#   FMOD_LIBRARIES

if(NOT FMOD_ROOT)
  if (DEFINED ENV{FMOD_ROOT})
    set(FMOD_ROOT "$ENV{FMOD_ROOT}")
  else()
    set(FMOD_ROOT "${CMAKE_SOURCE_DIR}/third_party/fmod")
  endif()
endif()

# Platform specific lib paths
if (WIN32)
  set(_FMOD_LIB_SUBDIR "api/core/lib/x64")
  set(_FMOD_STUDIO_LIB_SUBDIR "api/studio/lib/x64")
  set(_FMOD_CORE_LIB "fmod_vc")
  set(_FMOD_STUDIO_LIB "fmodstudio_vc")
elseif(APPLE)
  set(_FMOD_LIB_SUBDIR "api/core/lib")
  set(_FMOD_STUDIO_LIB_SUBDIR "api/studio/lib")
  set(_FMOD_CORE_LIB "fmod")
  set(_FMOD_STUDIO_LIB "fmodstudio")
else()
  set(_FMOD_LIB_SUBDIR "api/core/lib/x86_64")
  set(_FMOD_STUDIO_LIB_SUBDIR "api/studio/lib/x86_64")
  set(_FMOD_CORE_LIB "fmod")
  set(_FMOD_STUDIO_LIB "fmodstudio")
endif()

find_path(FMOD_CORE_INCLUDE_DIR
    NAMES fmod.hpp
    PATHS "${FMOD_ROOT}/api/core/inc"
    NO_DEFAULT_PATH
)
find_path(FMOD_STUDIO_INCLUDE_DIR
    NAMES fmod_studio.hpp
    PATHS "${FMOD_ROOT}/api/studio/inc"
    NO_DEFAULT_PATH
)

find_library(FMOD_CORE_LIBRARY
    NAMES ${_FMOD_CORE_LIB}
    PATHS "${FMOD_ROOT}/${_FMOD_LIB_SUBDIR}"
    NO_DEFAULT_PATH
)
find_library(FMOD_STUDIO_LIBRARY
    NAMES ${_FMOD_STUDIO_LIB}
    PATHS "${FMOD_ROOT}/${_FMOD_STUDIO_LIB_SUBDIR}"
    NO_DEFAULT_PATH
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FMOD
    FOUND_VAR FMOD_FOUND
    REQUIRED_VARS
      FMOD_CORE_INCLUDE_DIR
      FMOD_STUDIO_INCLUDE_DIR
      FMOD_CORE_LIBRARY
      FMOD_STUDIO_LIBRARY
)

if(NOT FMOD_FOUND)
  message(FATAL_ERROR
      "\n"
      "  FMOD SDK not found at: ${FMOD_ROOT}\n"
      "\n"
      "  ForgeEngine uses FMOD Studio for audio. The SDK is free for non-commercial\n"
      "  use but must be downloaded manually from https://www.fmod.com/download\n"
      "\n"
      "  After downloading, extract the SDK so the structure looks like:\n"
      "    third_party/fmod/api/core/inc/fmod.hpp\n"
      "    third_party/fmod/api/studio/inc/fmod_studio.hpp\n"
      "\n"
      "  Alternatively, set -DFMOD_ROOT=/path/to/your/fmod/sdk\n"
      "  or set the FMOD_ROOT environment variable.\n"
      "\n"
      "  To build without audio support, configure with -DFORGE_AUDIO_FMOD=OFF\n"
  )
endif()

if(FMOD_FOUND)
  set(FMOD_INCLUDE_DIRS
    "${FMOD_CORE_INCLUDE_DIR}"
    "${FMOD_STUDIO_INCLUDE_DIR}"
  )
  set(FMOD_LIBRARIES
    "${FMOD_CORE_LIBRARY}"
    "${FMOD_STUDIO_LIBRARY}"
  )

  if(NOT TARGET FMOD::Core)
    add_library(FMOD::Core SHARED IMPORTED)
    set_target_properties(FMOD::Core PROPERTIES
      IMPORTED_LOCATION "${FMOD_CORE_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${FMOD_CORE_INCLUDE_DIR}"
    )
    if(WIN32)
      find_file(_FMOD_CORE_DLL
        NAMES fmod.dll
        PATHS "${FMOD_ROOT}/${_FMOD_LIB_SUBDIR}"
        NO_DEFAULT_PATH
      )
      set_target_properties(FMOD::Core PROPERTIES
        IMPORTED_LOCATION "${_FMOD_CORE_DLL}"
        IMPORTED_IMPLIB "${FMOD_CORE_LIBRARY}"
      )
    endif()
  endif()

  if(NOT TARGET FMOD::Studio)
    add_library(FMOD::Studio SHARED IMPORTED)
    set_target_properties(FMOD::Studio PROPERTIES
      IMPORTED_LOCATION "${FMOD_STUDIO_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${FMOD_STUDIO_INCLUDE_DIR}"
    )
    if(WIN32)
      find_file(_FMOD_STUDIO_DLL
        NAMES fmod.dll
        PATHS "${FMOD_ROOT}/${_FMOD_STUDIO_LIB_SUBDIR}"
        NO_DEFAULT_PATH
      )
      set_target_properties(FMOD::Studio PROPERTIES
        IMPORTED_LOCATION "${_FMOD_STUDIO_DLL}"
        IMPORTED_IMPLIB "${FMOD_STUDIO_LIBRARY}"
      )
    endif()
  endif()
endif()

mark_as_advanced(
  FMOD_CORE_INCLUDE_DIR
  FMOD_STUDIO_INCLUDE_DIR
  FMOD_CORE_LIBRARY
  FMOD_STUDIO_LIBRARY
)

