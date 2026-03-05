set(S ${CMAKE_CURRENT_SOURCE_DIR})

set(QAGAME_SRC
  ${S}/g_main.c
  ${S}/ai_chat.c
  ${S}/ai_cmd.c
  ${S}/ai_dmnet.c
  ${S}/ai_dmq3.c
  ${S}/ai_main.c
  ${S}/ai_team.c
  ${S}/ai_vcmd.c
  ${S}/bg_misc.c
  ${S}/bg_pmove.c
  ${S}/bg_slidemove.c
  ${S}/g_active.c
  ${S}/g_arenas.c
  ${S}/g_bot.c
  ${S}/g_client.c
  ${S}/g_cmds.c
  ${S}/g_combat.c
  ${S}/g_items.c
  ${S}/g_mem.c
  ${S}/g_misc.c
  ${S}/g_missile.c
  ${S}/g_mover.c
  ${S}/g_session.c
  ${S}/g_spawn.c
  ${S}/g_svcmds.c
  ${S}/g_target.c
  ${S}/g_team.c
  ${S}/g_trigger.c
  ${S}/g_utils.c
  ${S}/g_weapon.c
  ${S}/q_math.c
  ${S}/q_shared.c
)

q3_add_game_module(
  NAME qagame
  SOURCES ${QAGAME_SRC}
  SO_SOURCES ${S}/g_syscalls.c
  INCLUDE_DIRS ${S} ${S}/../cgame ${S}/../q3_ui
  MISSIONPACK_INCLUDE_DIRS ${S} ${S}/../cgame ${S}/../ui
)
