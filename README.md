# BulletManager Assignment

This repository contains a C++ project that demonstrates bullet collision simulator in 2d

## Requirements

- CMake 3.20+
- Visual Studio 2022 with C++ desktop workload
- Git (required by CMake `FetchContent` to download raylib)

## Generate Visual Studio project files

From repository root:

1. `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`
2. `cmake --build build --config Release`

This config step downloads raylib automatically via CMake `FetchContent`.
Open `build/BulletManager.sln` in Visual Studio if you want to build/run from the IDE.
