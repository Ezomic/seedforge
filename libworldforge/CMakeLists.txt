# libworldforge: pure C++, header-only phases. Depends on the standard library only; tools/check_deps.py enforces it.
add_library(worldforge INTERFACE)
add_library(worldforge::worldforge ALIAS worldforge)
target_include_directories(worldforge INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(worldforge INTERFACE cxx_std_20)
