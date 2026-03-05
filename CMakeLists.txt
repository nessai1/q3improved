cmake_minimum_required(VERSION 3.16)
project(Quake3Arena VERSION 1.32 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 98)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Q3Platform)

# Build options
option(BUILD_CLIENT       "Build quake3 client"               ON)
option(BUILD_CLIENT_SMP   "Build quake3-smp client"           ON)
option(BUILD_SERVER       "Build q3ded dedicated server"      ON)
option(BUILD_GAME_MODULES "Build cgame/qagame/ui .so modules" ON)
option(BUILD_MISSIONPACK  "Build Team Arena modules"          ON)

add_subdirectory(code)
