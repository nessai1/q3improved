# Q3Platform.cmake
# Architecture detection, compiler flags, and game module helper for Quake III Arena

# ---------------------------------------------------------------------------
# Architecture detection
# ---------------------------------------------------------------------------
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i[3-6]86|x86)$")
  set(Q3_ARCH_X86 ON)
  set(Q3_ARCH_X86_64 OFF)
  set(Q3_ARCH_STRING "i386")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
  set(Q3_ARCH_X86 OFF)
  set(Q3_ARCH_X86_64 ON)
  set(Q3_ARCH_STRING "x86_64")
else()
  set(Q3_ARCH_X86 OFF)
  set(Q3_ARCH_X86_64 OFF)
  set(Q3_ARCH_STRING "${CMAKE_SYSTEM_PROCESSOR}")
endif()

message(STATUS "Q3: Architecture=${Q3_ARCH_STRING} (x86=${Q3_ARCH_X86}, x86_64=${Q3_ARCH_X86_64})")

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------
add_compile_options(-pipe -fsigned-char)

# Wall without Werror -- this 2005 codebase won't compile clean under modern GCC
add_compile_options(-Wall)

# GCC 10+ defaults to -fno-common; this 2005 code uses tentative definitions
# in headers (e.g. global vars in qgl, refimport_t re) that become multiple
# definitions without -fcommon
add_compile_options(-fcommon)

# Suppress warnings endemic to this vintage code
add_compile_options(
  -Wno-unused-but-set-variable
  -Wno-unused-variable
  -Wno-missing-braces
  -Wno-format-truncation
  -Wno-pointer-to-int-cast
  -Wno-int-to-pointer-cast
  -Wno-strict-aliasing
)

# Per-configuration flags
set(CMAKE_C_FLAGS_DEBUG "-g -O" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_RELEASE "-DNDEBUG -O2 -fomit-frame-pointer -ffast-math -fno-strict-aliasing" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}" CACHE STRING "" FORCE)

# Arch-specific optimization for release
if(Q3_ARCH_X86)
  string(APPEND CMAKE_C_FLAGS_RELEASE " -march=pentium3 -mtune=generic")
  string(APPEND CMAKE_CXX_FLAGS_RELEASE " -march=pentium3 -mtune=generic")
elseif(Q3_ARCH_X86_64)
  string(APPEND CMAKE_C_FLAGS_RELEASE " -march=x86-64 -mtune=generic")
  string(APPEND CMAKE_CXX_FLAGS_RELEASE " -march=x86-64 -mtune=generic")
endif()

# Default build type
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  message(STATUS "Q3: Defaulting to Release build")
endif()

# ---------------------------------------------------------------------------
# q3_add_game_module() -- reduces duplication across 6+ game module targets
# ---------------------------------------------------------------------------
#
# Usage:
#   q3_add_game_module(
#     NAME cgame
#     SOURCES file1.c file2.c ...
#     SO_SOURCES cg_syscalls.c
#     INCLUDE_DIRS dir1 dir2 ...
#     DEFINES CGAME
#     MISSIONPACK_EXTRA_SOURCES cg_newdraw.c ../ui/ui_shared.c
#     MISSIONPACK_INCLUDE_DIRS dir1 dir2 ...
#   )
#
function(q3_add_game_module)
  cmake_parse_arguments(MOD
    ""
    "NAME"
    "SOURCES;SO_SOURCES;INCLUDE_DIRS;DEFINES;MISSIONPACK_EXTRA_SOURCES;MISSIONPACK_INCLUDE_DIRS"
    ${ARGN}
  )

  # --- baseq3 variant ---
  set(_target "${MOD_NAME}${Q3_ARCH_STRING}")
  add_library(${_target} SHARED ${MOD_SOURCES} ${MOD_SO_SOURCES})
  target_include_directories(${_target} PRIVATE ${MOD_INCLUDE_DIRS})
  if(MOD_DEFINES)
    target_compile_definitions(${_target} PRIVATE ${MOD_DEFINES})
  endif()
  target_compile_options(${_target} PRIVATE -fPIC)
  target_link_libraries(${_target} PRIVATE dl m)
  set_target_properties(${_target} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${MOD_NAME}${Q3_ARCH_STRING}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/baseq3"
  )

  # --- missionpack variant ---
  if(BUILD_MISSIONPACK)
    set(_target_ta "${MOD_NAME}_ta_${Q3_ARCH_STRING}")

    # Use missionpack include dirs if provided, else fall back to base
    if(MOD_MISSIONPACK_INCLUDE_DIRS)
      set(_ta_includes ${MOD_MISSIONPACK_INCLUDE_DIRS})
    else()
      set(_ta_includes ${MOD_INCLUDE_DIRS})
    endif()

    add_library(${_target_ta} SHARED
      ${MOD_SOURCES} ${MOD_SO_SOURCES} ${MOD_MISSIONPACK_EXTRA_SOURCES}
    )
    target_include_directories(${_target_ta} PRIVATE ${_ta_includes})
    if(MOD_DEFINES)
      target_compile_definitions(${_target_ta} PRIVATE ${MOD_DEFINES})
    endif()
    target_compile_definitions(${_target_ta} PRIVATE MISSIONPACK)
    target_compile_options(${_target_ta} PRIVATE -fPIC)
    target_link_libraries(${_target_ta} PRIVATE dl m)
    set_target_properties(${_target_ta} PROPERTIES
      PREFIX ""
      # Same output name -- different directory
      OUTPUT_NAME "${MOD_NAME}${Q3_ARCH_STRING}"
      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/missionpack"
    )
  endif()
endfunction()
