cmake_minimum_required(VERSION 3.20)
project(catberry-mtools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(Threads REQUIRED)

# The engines from src/common and src/linux; the drawn window (gui.cpp,
# mtools.cpp) is the Windows build's.
file(GLOB COMMON src/common/*.cpp)
list(FILTER COMMON EXCLUDE REGEX "src/common/(gui|mtools)\\.cpp$")
file(GLOB LINUX src/linux/*.cpp)
file(GLOB QT src/qt/*.cpp)

add_executable(catberry-mtools src/main.cpp ${COMMON} ${LINUX} ${QT})
target_compile_options(catberry-mtools PRIVATE -Wall -Wextra)
target_link_libraries(catberry-mtools PRIVATE Qt6::Widgets Threads::Threads)

install(TARGETS catberry-mtools RUNTIME DESTINATION bin)
install(FILES data/catberry-mtools.desktop DESTINATION share/applications)
