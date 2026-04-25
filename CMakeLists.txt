cmake_minimum_required(VERSION 3.20)

project(BulletManager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# Download raylib at configure time.
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
)

FetchContent_MakeAvailable(raylib)

add_executable(BulletManager
    BulletManager/src/main.cpp
    BulletManager/src/BulletManager.cpp
    BulletManager/src/BulletManager.h
)

target_link_libraries(BulletManager PRIVATE raylib)

if(MSVC)
    target_compile_options(BulletManager PRIVATE /W4 /permissive-)
else()
    target_compile_options(BulletManager PRIVATE -Wall -Wextra -Wpedantic)
endif()
