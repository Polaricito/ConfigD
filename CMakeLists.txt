cmake_minimum_required(VERSION 3.16)
project(HyprSet VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_add_executable(hyprset
    src/main.cpp
    src/binds.cpp
    src/binds.h
    src/luaparser.cpp
    src/luaparser.h
    src/settings.cpp
    src/settings.h
    src/mainwindow.cpp
    src/mainwindow.h
)

target_link_libraries(hyprset PRIVATE Qt6::Widgets)