cmake_minimum_required(VERSION 3.30)

project(Loom LANGUAGES CXX)

set(LOOM_IMGUI_TARGET "" CACHE STRING "Existing Dear ImGui target to use instead of the pinned dependency")
if(LOOM_IMGUI_TARGET)
    if(NOT TARGET "${LOOM_IMGUI_TARGET}")
        message(FATAL_ERROR "LOOM_IMGUI_TARGET names a missing target: ${LOOM_IMGUI_TARGET}")
    endif()
else()
    if(NOT TARGET imgui)
        include(FetchContent)
        FetchContent_Declare(imgui
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG c1aaac65e896a45f81f47a4d68f9959e0f86c859 # v1.93.0
        )
        FetchContent_MakeAvailable(imgui)
        add_library(imgui STATIC
            "${imgui_SOURCE_DIR}/imgui.cpp"
            "${imgui_SOURCE_DIR}/imgui_draw.cpp"
            "${imgui_SOURCE_DIR}/imgui_tables.cpp"
            "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
        )
        target_include_directories(imgui SYSTEM PUBLIC "${imgui_SOURCE_DIR}")
    endif()
    set(LOOM_IMGUI_TARGET imgui)
endif()

file(GLOB LOOM_MODULES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cppm")
file(GLOB LOOM_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(loom STATIC)
target_sources(loom
    PRIVATE ${LOOM_SOURCES}
    PUBLIC FILE_SET CXX_MODULES
    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src"
    FILES ${LOOM_MODULES}
)
target_compile_features(loom PUBLIC cxx_std_23)
target_link_libraries(loom PUBLIC "${LOOM_IMGUI_TARGET}")
set_target_properties(loom PROPERTIES CXX_EXTENSIONS OFF)
