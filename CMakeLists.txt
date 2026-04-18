cmake_minimum_required(VERSION 3.27)

project(render-unfocused-fix
    DESCRIPTION "render-unfocused-fix plugin for Hyprland"
    VERSION 0.1
)

set(CMAKE_CXX_STANDARD 23)

file(GLOB_RECURSE SRC "*.cpp")

add_library(render-unfocused-fix SHARED ${SRC})

find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET
    hyprland
    libdrm
    libinput
    libudev
    pangocairo
    pixman-1
    wayland-server
    xkbcommon
)
target_link_libraries(render-unfocused-fix PRIVATE rt PkgConfig::deps)

install(TARGETS render-unfocused-fix)
