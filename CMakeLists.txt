cmake_minimum_required(VERSION 3.20)
project(catberry-roofce VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets Network)
find_package(LayerShellQt REQUIRED)

add_executable(catberry-roofce
    src/main.cpp
    src/paths.cpp
    src/settings.cpp
    src/library.cpp
    src/online.cpp
    src/wallpaper.cpp
    src/mainwindow.cpp
)
target_compile_definitions(catberry-roofce PRIVATE ROOFCE_VERSION="${PROJECT_VERSION}")
target_compile_options(catberry-roofce PRIVATE -Wall -Wextra)
target_link_libraries(catberry-roofce PRIVATE
    Qt6::Widgets Qt6::Multimedia Qt6::MultimediaWidgets Qt6::Network LayerShellQt::Interface)

install(TARGETS catberry-roofce RUNTIME DESTINATION bin)
