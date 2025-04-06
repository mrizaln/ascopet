cmake_minimum_required(VERSION 3.12)

project(
    ascopet
    VERSION     0.1.0
    LANGUAGES   CXX
    DESCRIPTION "Asynchronous scope time measurement library for C++20")

include(cmake/fetched-libs.cmake)

add_library(ascopet STATIC source/ascopet.cpp)
target_include_directories(ascopet PUBLIC include)
target_link_libraries(ascopet PUBLIC fetch::circbuf)
target_compile_features(ascopet PUBLIC cxx_std_20)
set_target_properties(ascopet PROPERTIES CXX_EXTENSIONS OFF)
