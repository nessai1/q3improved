set(S ${CMAKE_CURRENT_SOURCE_DIR})

set(Q3UI_SRC
  ${S}/ui_main.c
  ${S}/../game/bg_misc.c
  ${S}/../game/q_math.c
  ${S}/../game/q_shared.c
  ${S}/ui_addbots.c
  ${S}/ui_atoms.c
  ${S}/ui_cdkey.c
  ${S}/ui_cinematics.c
  ${S}/ui_confirm.c
  ${S}/ui_connect.c
  ${S}/ui_controls2.c
  ${S}/ui_credits.c
  ${S}/ui_demo2.c
  ${S}/ui_display.c
  ${S}/ui_gameinfo.c
  ${S}/ui_ingame.c
  ${S}/ui_menu.c
  ${S}/ui_mfield.c
  ${S}/ui_mods.c
  ${S}/ui_network.c
  ${S}/ui_options.c
  ${S}/ui_playermodel.c
  ${S}/ui_players.c
  ${S}/ui_playersettings.c
  ${S}/ui_preferences.c
  ${S}/ui_qmenu.c
  ${S}/ui_removebots.c
  ${S}/ui_serverinfo.c
  ${S}/ui_servers2.c
  ${S}/ui_setup.c
  ${S}/ui_sound.c
  ${S}/ui_sparena.c
  ${S}/ui_specifyserver.c
  ${S}/ui_splevel.c
  ${S}/ui_sppostgame.c
  ${S}/ui_spskill.c
  ${S}/ui_startserver.c
  ${S}/ui_team.c
  ${S}/ui_teamorders.c
  ${S}/ui_video.c
)

# baseq3 UI -- no missionpack variant (TA has its own separate UI in code/ui/)
add_library(ui${Q3_ARCH_STRING} SHARED
  ${Q3UI_SRC}
  ${S}/../ui/ui_syscalls.c
)
target_include_directories(ui${Q3_ARCH_STRING} PRIVATE
  ${S}
  ${S}/../game
  ${S}/../cgame
  ${S}/../ui
)
target_compile_options(ui${Q3_ARCH_STRING} PRIVATE -fPIC)
target_link_libraries(ui${Q3_ARCH_STRING} PRIVATE dl m)
set_target_properties(ui${Q3_ARCH_STRING} PROPERTIES
  PREFIX ""
  OUTPUT_NAME "ui${Q3_ARCH_STRING}"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/baseq3"
)
