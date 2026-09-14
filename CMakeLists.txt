cmake_minimum_required(VERSION 3.16)
project(HyprChange VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_add_executable(hyprchange
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

qt_add_resources(hyprchange "res"
    PREFIX "/"
    BASE "resources"
    FILES resources/logo.png
)

target_link_libraries(hyprchange PRIVATE Qt6::Widgets)