set(S ${CMAKE_CURRENT_SOURCE_DIR})

set(CGAME_SRC
  ${S}/cg_main.c
  ${S}/../game/bg_misc.c
  ${S}/../game/bg_pmove.c
  ${S}/../game/bg_slidemove.c
  ${S}/../game/q_math.c
  ${S}/../game/q_shared.c
  ${S}/cg_consolecmds.c
  ${S}/cg_draw.c
  ${S}/cg_drawtools.c
  ${S}/cg_effects.c
  ${S}/cg_ents.c
  ${S}/cg_event.c
  ${S}/cg_info.c
  ${S}/cg_localents.c
  ${S}/cg_marks.c
  ${S}/cg_players.c
  ${S}/cg_playerstate.c
  ${S}/cg_predict.c
  ${S}/cg_scoreboard.c
  ${S}/cg_servercmds.c
  ${S}/cg_snapshot.c
  ${S}/cg_view.c
  ${S}/cg_weapons.c
)

q3_add_game_module(
  NAME cgame
  SOURCES ${CGAME_SRC}
  SO_SOURCES ${S}/cg_syscalls.c
  INCLUDE_DIRS ${S} ${S}/../game ${S}/../q3_ui
  DEFINES CGAME
  MISSIONPACK_EXTRA_SOURCES
    ${S}/cg_newdraw.c
    ${S}/../ui/ui_shared.c
  MISSIONPACK_INCLUDE_DIRS ${S} ${S}/../game ${S}/../ui
)
