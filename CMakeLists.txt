cmake_minimum_required(VERSION 3.16)

project(QtApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(QtApp WIN32
    main.cpp
)

target_link_libraries(QtApp PRIVATE Qt6::Widgets)