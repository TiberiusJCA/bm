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

    // Spawn a dense random wall field while keeping a clear corridor near x = 0,
    // which is where bullets are spawned.
    {
        std::mt19937 wallRng(424242u);
        std::uniform_real_distribution<float> yDist(-20.0f, 20.0f);
        std::uniform_real_distribution<float> xLeftDist(-20.0f, -2.0f);
        std::uniform_real_distribution<float> xRightDist(2.0f, 20.0f);
        std::uniform_real_distribution<float> angleDist(0.0f, 6.283185307f);
        std::uniform_real_distribution<float> lengthDist(0.6f, 2.8f);
        std::bernoulli_distribution sideDist(0.5);

        constexpr int kWallCount = 10000;
        for (int i = 0; i < kWallCount; ++i) {
            const bool onLeftSide = sideDist(wallRng);
            const float cx = onLeftSide ? xLeftDist(wallRng) : xRightDist(wallRng);
            const float cy = yDist(wallRng);
            const float angle = angleDist(wallRng);
            const float halfLength = 0.5f * lengthDist(wallRng);
            const float dx = std::cos(angle) * halfLength;
            const float dy = std::sin(angle) * halfLength;

            manager.AddWall({cx - dx, cy - dy}, {cx + dx, cy + dy});
        }
    }

    std::atomic<bool> stopWorkers = false;
    std::atomic<bool> startFiring = false;
    bool simulationPaused = false;
    std::vector<std::thread> workers;

    for (int i = 0; i < 5; ++i) {
        workers.emplace_back([i, &manager, &stopWorkers, &startFiring]() {
            std::mt19937 rng(1337u + static_cast<unsigned>(i));
            std::uniform_real_distribution<float> angleDist(0.0f, 6.283185307f);
            std::uniform_real_distribution<float> yOffsetDist(-20.0f, 20.0f);
            std::uniform_real_distribution<float> speedDist(8.0f, 16.0f);
            // With 5 worker threads and 8s lifetime, this cadence targets ~10,000 live bullets.
            std::uniform_real_distribution<float> pauseDist(0.0035f, 0.0045f);

            while (!stopWorkers.load(std::memory_order_relaxed)) {
                if (!startFiring.load(std::memory_order_relaxed)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }

                const float angle = angleDist(rng);
                const float2 dir = {std::cos(angle), std::sin(angle)};
                const float2 start = {0.0f, yOffsetDist(rng)};
                const float speed = speedDist(rng);
                const float now = static_cast<float>(GetTime());
                manager.Fire(start, dir, speed, now, 8.0f);
                std::this_thread::sleep_for(std::chrono::duration<float>(pauseDist(rng)));
            }
        });
    }

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_ENTER)) {
            startFiring.store(true, std::memory_order_relaxed);
        }
        if (IsKeyPressed(KEY_SPACE)) {
            simulationPaused = !simulationPaused;
        }

        const float now = static_cast<float>(GetTime());
        if (!simulationPaused) {
            manager.Update(now);
        }

        BeginDrawing();
        ClearBackground(BLACK);

        const int fps = GetFPS();
        const Color fpsColor = (fps < 60) ? ORANGE : RAYWHITE;
        DrawText(TextFormat("FPS: %d", fps), 20, 20, 20, fpsColor);

        const std::vector<Wall> walls = manager.GetWalls();
        for (const Wall& wall : walls) {
            DrawLineV(ToScreen(wall.a), ToScreen(wall.b), VIOLET);
        }

        const std::vector<float2> bullets = manager.GetBulletPositions();
        DrawText(TextFormat("Alive bullets: %i  Walls: %i", static_cast<int>(bullets.size()), static_cast<int>(walls.size())), 20, 50, 20, LIGHTGRAY);
        if (!startFiring.load(std::memory_order_relaxed)) {
            DrawText("Press ENTER to start bullet spawning", 20, 80, 20, YELLOW);
        }
        if (simulationPaused) {
            DrawText("Simulation paused (press SPACE to resume)", 20, 110, 20, ORANGE);
        }
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
