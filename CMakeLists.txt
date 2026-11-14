cmake_minimum_required(VERSION 3.20)
project(catberry-roofce VERSION 1.2.7 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets Network DBus)
find_package(LayerShellQt REQUIRED)

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
    src/mainwindow.cpp
)
target_compile_definitions(catberry-roofce PRIVATE ROOFCE_VERSION="${PROJECT_VERSION}")
target_compile_options(catberry-roofce PRIVATE -Wall -Wextra)
target_link_libraries(catberry-roofce PRIVATE
    Qt6::Widgets Qt6::Multimedia Qt6::MultimediaWidgets Qt6::Network Qt6::DBus LayerShellQt::Interface)

install(TARGETS catberry-roofce RUNTIME DESTINATION bin)

# Unit tests: make test (packages build from a copy without the tests folder)
if(EXISTS "${CMAKE_SOURCE_DIR}/tests/test_online.cpp")
    add_executable(roofce-tests EXCLUDE_FROM_ALL tests/test_online.cpp src/online.cpp src/linkimport.cpp src/paths.cpp)
    target_compile_definitions(roofce-tests PRIVATE TESTS_DIR="${CMAKE_SOURCE_DIR}/tests" ROOFCE_VERSION="${PROJECT_VERSION}")
    target_link_libraries(roofce-tests PRIVATE Qt6::Widgets Qt6::Network)
endif()
