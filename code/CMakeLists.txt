# ===========================================================================
# code/CMakeLists.txt -- static libraries, executables, game module dispatch
# ===========================================================================

# --- Static libraries ---
add_subdirectory(jpeg-6)
add_subdirectory(botlib)
add_subdirectory(splines)

# --- Find system dependencies ---
if(BUILD_CLIENT)
  # GLFW for window management and Vulkan surface
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  add_subdirectory(/home/daniel/repos/github/glfw ${CMAKE_BINARY_DIR}/glfw)
  find_package(Vulkan REQUIRED)
  find_package(SDL2 REQUIRED)
endif()

# ===========================================================================
# Source lists (verified against Conscript-client / Conscript-dedicated)
# ===========================================================================

set(S ${CMAKE_CURRENT_SOURCE_DIR})

# --- qcommon ---
set(QCOMMON_SRC
  ${S}/qcommon/cm_load.c
  ${S}/qcommon/cm_patch.c
  ${S}/qcommon/cm_polylib.c
  ${S}/qcommon/cm_test.c
  ${S}/qcommon/cm_trace.c
  ${S}/qcommon/cmd.c
  ${S}/qcommon/common.c
  ${S}/qcommon/cvar.c
  ${S}/qcommon/files.c
  ${S}/qcommon/huffman.c
  ${S}/qcommon/md4.c
  ${S}/qcommon/msg.c
  ${S}/qcommon/net_chan.c
  ${S}/qcommon/unzip.c
)

# --- VM ---
set(VM_SRC
  ${S}/qcommon/vm.c
  ${S}/qcommon/vm_interpreted.c
)
if(Q3_ARCH_X86)
  # 32-bit x86: real JIT compiler
  list(APPEND VM_SRC ${S}/qcommon/vm_x86.c)
else()
  # x86_64 / other: stub that falls back to interpreter
  list(APPEND VM_SRC ${S}/unix/vm_x86.c)
endif()

# --- server ---
set(SERVER_SRC
  ${S}/server/sv_bot.c
  ${S}/server/sv_ccmds.c
  ${S}/server/sv_client.c
  ${S}/server/sv_game.c
  ${S}/server/sv_init.c
  ${S}/server/sv_main.c
  ${S}/server/sv_net_chan.c
  ${S}/server/sv_snapshot.c
  ${S}/server/sv_world.c
)

# --- client ---
set(CLIENT_SRC
  ${S}/client/cl_cgame.c
  ${S}/client/cl_cin.c
  ${S}/client/cl_console.c
  ${S}/client/cl_input.c
  ${S}/client/cl_keys.c
  ${S}/client/cl_main.c
  ${S}/client/cl_net_chan.c
  ${S}/client/cl_parse.c
  ${S}/client/cl_scrn.c
  ${S}/client/cl_ui.c
)

# --- sound ---
set(SOUND_SRC
  ${S}/client/snd_adpcm.c
  ${S}/client/snd_dma.c
  ${S}/client/snd_mem.c
  ${S}/client/snd_mix.c
  ${S}/client/snd_wavelet.c
)

# --- renderer ---
set(RENDERER_SRC
  ${S}/renderer/tr_animation.c
  ${S}/renderer/tr_backend.c
  ${S}/renderer/tr_bsp.c
  ${S}/renderer/tr_cmds.c
  ${S}/renderer/tr_curve.c
  ${S}/renderer/tr_flares.c
  ${S}/renderer/tr_font.c
  ${S}/renderer/tr_image.c
  ${S}/renderer/tr_init.c
  ${S}/renderer/tr_light.c
  ${S}/renderer/tr_main.c
  ${S}/renderer/tr_marks.c
  ${S}/renderer/tr_mesh.c
  ${S}/renderer/tr_model.c
  ${S}/renderer/tr_noise.c
  ${S}/renderer/tr_scene.c
  ${S}/renderer/tr_shade.c
  ${S}/renderer/tr_shade_calc.c
  ${S}/renderer/tr_shader.c
  ${S}/renderer/tr_shadows.c
  ${S}/renderer/tr_sky.c
  ${S}/renderer/tr_surface.c
  ${S}/renderer/tr_world.c
  ${S}/renderer/vk_init.c
  ${S}/renderer/vk_pipeline.c
  ${S}/renderer/vk_image.c
  ${S}/renderer/vk_shade.c
  ${S}/renderer/qgl_stubs.c
)

# --- unix platform (client) ---
set(UNIX_CLIENT_SRC
  ${S}/unix/unix_main.c
  ${S}/unix/unix_net.c
  ${S}/unix/unix_shared.c
  ${S}/unix/linux_common.c
  ${S}/unix/linux_glimp.c
  ${S}/unix/linux_joystick.c
  ${S}/unix/linux_snd.c
  ${S}/unix/linux_signals.c
)

# --- unix platform (dedicated -- subset) ---
set(UNIX_SERVER_SRC
  ${S}/unix/unix_main.c
  ${S}/unix/unix_net.c
  ${S}/unix/unix_shared.c
  ${S}/unix/linux_common.c
  ${S}/unix/linux_signals.c
)

# --- null stubs for dedicated server ---
set(NULL_SRC
  ${S}/null/null_client.c
  ${S}/null/null_input.c
  ${S}/null/null_snddma.c
)

# --- shared game code ---
set(GAME_SHARED_SRC
  ${S}/game/q_shared.c
  ${S}/game/q_math.c
)

# --- Compile all engine C sources as C++ ---
set(ALL_ENGINE_C_SRC
  ${RENDERER_SRC} ${CLIENT_SRC} ${SOUND_SRC}
  ${QCOMMON_SRC} ${VM_SRC} ${SERVER_SRC}
  ${UNIX_CLIENT_SRC} ${UNIX_SERVER_SRC} ${NULL_SRC}
  ${GAME_SHARED_SRC}
)
set_source_files_properties(${ALL_ENGINE_C_SRC} PROPERTIES LANGUAGE CXX)

# Common include directories for engine targets
set(ENGINE_INCLUDES
  ${S}
  ${S}/game
  ${S}/qcommon
  ${S}/renderer
  ${S}/client
  ${S}/server
  ${S}/unix
  ${S}/botlib
  ${S}/jpeg-6
  ${S}/splines
)

# ===========================================================================
# Client executable
# ===========================================================================
# ===========================================================================
# SPIR-V shader compilation
# ===========================================================================
if(BUILD_CLIENT)
  set(SHADER_DIR ${S}/renderer/shaders)
  set(SPV_OUTPUT_DIR ${CMAKE_BINARY_DIR}/baseq3/shaders)
  file(MAKE_DIRECTORY ${SPV_OUTPUT_DIR})

  add_custom_command(
    OUTPUT ${SPV_OUTPUT_DIR}/generic.vert.spv
    COMMAND Vulkan::glslc -o ${SPV_OUTPUT_DIR}/generic.vert.spv ${SHADER_DIR}/generic.vert
    DEPENDS ${SHADER_DIR}/generic.vert
    COMMENT "Compiling generic.vert -> SPIR-V"
  )
  add_custom_command(
    OUTPUT ${SPV_OUTPUT_DIR}/generic.frag.spv
    COMMAND Vulkan::glslc -o ${SPV_OUTPUT_DIR}/generic.frag.spv ${SHADER_DIR}/generic.frag
    DEPENDS ${SHADER_DIR}/generic.frag
    COMMENT "Compiling generic.frag -> SPIR-V"
  )
  add_custom_target(shaders ALL DEPENDS
    ${SPV_OUTPUT_DIR}/generic.vert.spv
    ${SPV_OUTPUT_DIR}/generic.frag.spv
  )
endif()

if(BUILD_CLIENT)
  add_executable(quake3
    ${RENDERER_SRC}
    ${CLIENT_SRC}
    ${SOUND_SRC}
    ${QCOMMON_SRC}
    ${VM_SRC}
    ${SERVER_SRC}
    ${UNIX_CLIENT_SRC}
    ${GAME_SHARED_SRC}
  )
  target_include_directories(quake3 PRIVATE ${ENGINE_INCLUDES} ${SDL2_INCLUDE_DIRS})
  add_dependencies(quake3 shaders)
  target_link_libraries(quake3 PRIVATE
    q3_splines q3_botlib q3_jpeglib
    glfw
    Vulkan::Vulkan
    ${SDL2_LIBRARIES}
    ${CMAKE_DL_LIBS} m pthread
  )
  set_target_properties(quake3 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
endif()

# ===========================================================================
# SMP Client executable
# ===========================================================================
if(BUILD_CLIENT AND BUILD_CLIENT_SMP)
  add_executable(quake3-smp
    ${RENDERER_SRC}
    ${CLIENT_SRC}
    ${SOUND_SRC}
    ${QCOMMON_SRC}
    ${VM_SRC}
    ${SERVER_SRC}
    ${UNIX_CLIENT_SRC}
    ${GAME_SHARED_SRC}
  )
  target_include_directories(quake3-smp PRIVATE ${ENGINE_INCLUDES} ${SDL2_INCLUDE_DIRS})
  target_compile_definitions(quake3-smp PRIVATE SMP)
  add_dependencies(quake3-smp shaders)
  target_link_libraries(quake3-smp PRIVATE
    q3_splines q3_botlib q3_jpeglib
    glfw
    Vulkan::Vulkan
    ${SDL2_LIBRARIES}
    ${CMAKE_DL_LIBS} m pthread
  )
  set_target_properties(quake3-smp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
endif()

# ===========================================================================
# Dedicated server
# ===========================================================================
if(BUILD_SERVER)
  add_executable(q3ded
    ${SERVER_SRC}
    ${QCOMMON_SRC}
    ${VM_SRC}
    ${UNIX_SERVER_SRC}
    ${NULL_SRC}
    ${GAME_SHARED_SRC}
  )
  target_include_directories(q3ded PRIVATE
    ${S}
    ${S}/game
    ${S}/qcommon
    ${S}/server
    ${S}/unix
    ${S}/botlib
    ${S}/null
  )
  target_compile_definitions(q3ded PRIVATE DEDICATED C_ONLY)
  target_link_libraries(q3ded PRIVATE
    q3_botlib
    ${CMAKE_DL_LIBS} m
  )
  set_target_properties(q3ded PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
endif()

# ===========================================================================
# Game modules (.so)
# ===========================================================================
if(BUILD_GAME_MODULES)
  add_subdirectory(cgame)
  add_subdirectory(game)
  add_subdirectory(q3_ui)
  if(BUILD_MISSIONPACK)
    add_subdirectory(ui)
  endif()
endif()
