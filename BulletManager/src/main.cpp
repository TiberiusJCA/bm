#include "BulletManager.h"

#include "raylib.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <random>
#include <thread>
#include <vector>

namespace {
constexpr int kScreenWidth = 1280;
constexpr int kScreenHeight = 720;
constexpr float kPixelsPerMeter = 30.0f;

Vector2 ToScreen(float2 p) {
    return {
        static_cast<float>(kScreenWidth) * 0.5f + p.x * kPixelsPerMeter,
        static_cast<float>(kScreenHeight) * 0.5f - p.y * kPixelsPerMeter};
}
}

int main() {
    InitWindow(kScreenWidth, kScreenHeight, "BulletManager assignment");
    SetTargetFPS(60);

    BulletManager manager;

    // A few sample walls. Any bullet hit removes the wall and continues reflected.
    manager.AddWall({-10.0f, 7.0f}, {10.0f, 7.0f});
    manager.AddWall({-14.0f, -2.0f}, {-2.0f, 5.0f});
    manager.AddWall({3.0f, -6.0f}, {13.0f, -2.0f});
    manager.AddWall({-12.0f, -8.0f}, {12.0f, -8.0f});

    std::atomic<bool> stopWorkers = false;
    std::vector<std::thread> workers;

    for (int i = 0; i < 2; ++i) {
        workers.emplace_back([i, &manager, &stopWorkers]() {
            std::mt19937 rng(1337u + static_cast<unsigned>(i));
            std::uniform_real_distribution<float> angleDist(0.0f, 6.283185307f);
            std::uniform_real_distribution<float> yOffsetDist(-4.0f, 4.0f);
            std::uniform_real_distribution<float> speedDist(8.0f, 16.0f);
            std::uniform_real_distribution<float> pauseDist(0.10f, 0.35f);

            while (!stopWorkers.load(std::memory_order_relaxed)) {
                const float angle = angleDist(rng);
                const float2 dir = {std::cos(angle), std::sin(angle)};
                const float2 start = {-16.0f + static_cast<float>(i) * 2.0f, yOffsetDist(rng)};
                const float speed = speedDist(rng);
                const float now = static_cast<float>(GetTime());
                manager.Fire(start, dir, speed, now, 8.0f);
                std::this_thread::sleep_for(std::chrono::duration<float>(pauseDist(rng)));
            }
        });
    }

    while (!WindowShouldClose()) {
        const float now = static_cast<float>(GetTime());
        manager.Update(now);

        BeginDrawing();
        ClearBackground(BLACK);

        DrawText("Fire() runs from worker threads in parallel with Update()", 20, 20, 20, RAYWHITE);
        DrawText("Walls disappear on hit, bullet keeps moving with reflected direction", 20, 50, 20, LIGHTGRAY);

        const std::vector<Wall> walls = manager.GetWalls();
        for (const Wall& wall : walls) {
            DrawLineV(ToScreen(wall.a), ToScreen(wall.b), ORANGE);
        }

        const std::vector<float2> bullets = manager.GetBulletPositions();
        for (const float2& p : bullets) {
            DrawCircleV(ToScreen(p), 4.0f, SKYBLUE);
        }

        EndDrawing();
    }

    stopWorkers.store(true, std::memory_order_relaxed);
    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    CloseWindow();
    return 0;
}
