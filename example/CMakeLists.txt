add_executable(trace source/trace.cpp)
target_include_directories(trace PRIVATE source)
target_link_libraries(trace PRIVATE ascopet)
target_compile_options(trace PRIVATE -Wall -Wextra -Wconversion)

target_compile_features(ascopet PUBLIC cxx_std_20)
set_target_properties(ascopet PROPERTIES CXX_EXTENSIONS OFF)

## sanitizer
#target_compile_options(trace PRIVATE -fsanitize=address,leak,undefined)
#target_link_options(trace PRIVATE -fsanitize=address,leak,undefined)
