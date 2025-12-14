cmake_minimum_required(VERSION 3.12)

project(
  ascopet
  VERSION 0.3.2
  LANGUAGES CXX
  DESCRIPTION "Asynchronous scope time measurement library for C++20"
)

string(
  COMPARE EQUAL
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}"
  ASCOPET_STANDALONE
)

option(ASCOPET_BUILD_EXAMPLES "Build example programs" ${ASCOPET_STANDALONE})
option(ASCOPET_DISABLE_RDTSC "Disable rdtsc" OFF)

add_library(ascopet STATIC source/ascopet.cpp)
target_include_directories(ascopet PUBLIC include)
target_compile_features(ascopet PUBLIC cxx_std_20)
set_target_properties(ascopet PROPERTIES CXX_EXTENSIONS OFF)

if(ASCOPET_DISABLE_RDTSC)
  message(STATUS "ascopet: ASCOPET_DISABLE_RDTSC option set - disable rdtsc.")
  target_compile_definitions(ascopet PRIVATE ASCOPET_DISABLE_RDTSC)
endif()

if(ASCOPET_BUILD_EXAMPLES)
  add_subdirectory(example)
endif()
