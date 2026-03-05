set(S ${CMAKE_CURRENT_SOURCE_DIR})

# Team Arena UI (missionpack)
add_library(ui_ta_${Q3_ARCH_STRING} SHARED
  ${S}/ui_main.c
  ${S}/ui_atoms.c
  ${S}/ui_gameinfo.c
  ${S}/ui_players.c
  ${S}/ui_util.c
  ${S}/ui_shared.c
  ${S}/../game/bg_misc.c
  ${S}/../game/q_math.c
  ${S}/../game/q_shared.c
  ${S}/ui_syscalls.c
)
target_include_directories(ui_ta_${Q3_ARCH_STRING} PRIVATE
  ${S}
  ${S}/../game
  ${S}/../cgame
)
target_compile_definitions(ui_ta_${Q3_ARCH_STRING} PRIVATE MISSIONPACK)
target_compile_options(ui_ta_${Q3_ARCH_STRING} PRIVATE -fPIC)
target_link_libraries(ui_ta_${Q3_ARCH_STRING} PRIVATE dl m)
set_target_properties(ui_ta_${Q3_ARCH_STRING} PROPERTIES
  PREFIX ""
  OUTPUT_NAME "ui${Q3_ARCH_STRING}"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/missionpack"
)
