cmake_minimum_required(VERSION 3.30)

project(Loom LANGUAGES CXX)

file(GLOB LOOM_MODULES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cppm")

add_library(loom STATIC)
target_sources(loom
    PUBLIC FILE_SET CXX_MODULES
    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src"
    FILES ${LOOM_MODULES}
)
target_compile_features(loom PUBLIC cxx_std_23)
set_target_properties(loom PROPERTIES CXX_EXTENSIONS OFF)
