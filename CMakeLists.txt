cmake_minimum_required(VERSION 3.20)
project(catberry-roofce VERSION 1.2.8 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets Network DBus)
find_package(LayerShellQt REQUIRED)

# To know when another app window is open (so the wallpaper can mute its
# sound): KDE's plasma-window-management and the wlroots foreign-toplevel
# protocol. wayland-scanner turns the vendored XML into the client glue.
find_package(PkgConfig REQUIRED)
pkg_check_modules(WaylandClient REQUIRED IMPORTED_TARGET wayland-client)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

set(WAYLAND_GLUE "")
function(roofce_wayland_protocol xml stem)
    set(header ${CMAKE_BINARY_DIR}/${stem}-client-protocol.h)
    set(code ${CMAKE_BINARY_DIR}/${stem}-protocol.c)
    add_custom_command(OUTPUT ${header}
        COMMAND ${WAYLAND_SCANNER} client-header ${xml} ${header} DEPENDS ${xml} VERBATIM)
    add_custom_command(OUTPUT ${code}
        COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${code} DEPENDS ${xml} VERBATIM)
    set(WAYLAND_GLUE ${WAYLAND_GLUE} ${code} ${header} PARENT_SCOPE)
endfunction()
roofce_wayland_protocol(${CMAKE_SOURCE_DIR}/protocols/wlr-foreign-toplevel-management-unstable-v1.xml
    wlr-foreign-toplevel-management-unstable-v1)
roofce_wayland_protocol(${CMAKE_SOURCE_DIR}/protocols/plasma-window-management.xml
    plasma-window-management)

add_executable(catberry-roofce
    src/main.cpp
    src/paths.cpp
    src/settings.cpp
    src/library.cpp
    src/online.cpp
    src/linkimport.cpp
    src/wallpaper.cpp
    src/widgets.cpp
    src/track.cpp
    src/fonts.cpp
    src/foreground.cpp
    src/mainwindow.cpp
    ${WAYLAND_GLUE}
)
target_compile_definitions(catberry-roofce PRIVATE ROOFCE_VERSION="${PROJECT_VERSION}")
target_compile_options(catberry-roofce PRIVATE -Wall -Wextra)
target_include_directories(catberry-roofce PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries(catberry-roofce PRIVATE
    Qt6::Widgets Qt6::Multimedia Qt6::MultimediaWidgets Qt6::Network Qt6::DBus LayerShellQt::Interface
    PkgConfig::WaylandClient)

install(TARGETS catberry-roofce RUNTIME DESTINATION bin)

# Unit tests: make test (packages build from a copy without the tests folder)
if(EXISTS "${CMAKE_SOURCE_DIR}/tests/test_online.cpp")
    add_executable(roofce-tests EXCLUDE_FROM_ALL tests/test_online.cpp src/online.cpp src/linkimport.cpp src/paths.cpp)
    target_compile_definitions(roofce-tests PRIVATE TESTS_DIR="${CMAKE_SOURCE_DIR}/tests" ROOFCE_VERSION="${PROJECT_VERSION}")
    target_link_libraries(roofce-tests PRIVATE Qt6::Widgets Qt6::Network)
endif()
