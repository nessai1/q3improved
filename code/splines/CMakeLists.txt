add_library(q3_splines STATIC
  math_angles.cpp
  math_matrix.cpp
  math_quaternion.cpp
  math_vector.cpp
  q_parse.cpp
  q_shared.cpp
  splines.cpp
  util_str.cpp
)
target_include_directories(q3_splines PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
