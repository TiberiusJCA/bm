# BulletManager Assignment

This repository contains a C++ project that demonstrates:

- `void Update(float time)` on the main thread
- `void Fire(float2 pos, float2 dir, float speed, float time, float life_time)` from worker threads in parallel with `Update`
- Bullet-wall collision behavior where a wall is removed and bullet trajectory is reflected

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
