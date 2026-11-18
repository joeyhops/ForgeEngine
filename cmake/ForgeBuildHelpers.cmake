include_guard(GLOBAL)

function(forge_link_engine target)
  target_link_libraries(${target} PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,forge_engine>")
endfunction()

option(FORGE_ENABLE_WARNINGS "Enable compiler warnings on first-party targets" ON)
option(FORGE_WARNINGS_AS_ERRORS "Treat all warnings as errors on first party targets" OFF)

function(forge_enable_warnings target)
  if(NOT FORGE_ENABLE_WARNINGS)
    return()
  endif()

  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
    if(FORGE_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall
      -Wextra
      -Wno-unused-parameter
    )
    if(FORGE_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

set(FORGE_SANITIZE "" CACHE STRING
    "Sanitizers to enable: '' | address | address,undefined | thread")

macro(forge_apply_sanitizers)
  if(NOT FORGE_SANITIZE STREQUAL "")
    message(STATUS "ForgeEngine: sanitizers enabled: ${FORGE_SANITIZE}")
    if(MSVC)
      if(FORGE_SANITIZE MATCHES "address")
        add_compile_options(/fsanitize=address)
      else()
        message(WARNING "ForgeEngine: MSVC supports only address sanitizer")
      endif()
    else()
      add_compile_options(
        -fsanitize=${FORGE_SANITIZE}
        -fno-omit-frame-pointer
        -fno-sanitize-recover=all
      )
      add_link_options(-fsanitize=${FORGE_SANITIZE})
    endif()
  endif()
endmacro()
