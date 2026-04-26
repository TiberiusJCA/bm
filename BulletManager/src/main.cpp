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
constexpr float kMaxBulletSpeed = 5.0f;
constexpr float kGridCellSize = 1.0f;

Vector2 ToScreen(float2 p) {
    return {
        static_cast<float>(kScreenWidth) * 0.5f + p.x * kPixelsPerMeter,
        static_cast<float>(kScreenHeight) * 0.5f - p.y * kPixelsPerMeter};
}

void Draw(BulletManager& manager, bool isFiring, bool simulationPaused) {
    PROFILE_ZONE_SCOPED("MainLoop::Draw");

    PROFILE_ZONE_BEGIN(BeginDrawingZone, "MainLoop::Draw / BeginDrawing");
    BeginDrawing();
    PROFILE_ZONE_END(BeginDrawingZone);

    ClearBackground(BLACK);

    const int fps = GetFPS();
    const Color fpsColor = (fps < 60) ? ORANGE : RAYWHITE;
    DrawText(TextFormat("FPS: %d", fps), 20, 20, 20, fpsColor);

    PROFILE_ZONE_BEGIN(drawWallsZone, "MainLoop::Draw / Draw Walls");
    const std::vector<Wall> walls = manager.GetWalls();
    for (const Wall& wall : walls) {
        DrawLineV(ToScreen(wall.a), ToScreen(wall.b), VIOLET);
    }

    float2 gridBoundsMin = {};
    float2 gridBoundsMax = {};
    int gridWidth = 0;
    int gridHeight = 0;
    float gridCellSize = 0.0f;
    const bool hasGrid = manager.GetWallSpatialGridInfo(gridBoundsMin, gridBoundsMax, gridWidth, gridHeight, gridCellSize);
    if (hasGrid) {
        const Vector2 topLeft = ToScreen({gridBoundsMin.x, gridBoundsMax.y});
        const Vector2 bottomRight = ToScreen({gridBoundsMax.x, gridBoundsMin.y});
        const int rectX = static_cast<int>(topLeft.x);
        const int rectY = static_cast<int>(topLeft.y);
        const int rectW = static_cast<int>(bottomRight.x - topLeft.x);
        const int rectH = static_cast<int>(bottomRight.y - topLeft.y);
        DrawRectangleLines(rectX, rectY, rectW, rectH, DARKGREEN);

        for (int x = 1; x < gridWidth; ++x) {
            const float worldX = gridBoundsMin.x + static_cast<float>(x) * gridCellSize;
            const Vector2 a = ToScreen({worldX, gridBoundsMax.y});
            const Vector2 b = ToScreen({worldX, gridBoundsMin.y});
            DrawLineV(a, b, Fade(DARKGREEN, 0.35f));
        }

        for (int y = 1; y < gridHeight; ++y) {
            const float worldY = gridBoundsMin.y + static_cast<float>(y) * gridCellSize;
            const Vector2 a = ToScreen({gridBoundsMin.x, worldY});
            const Vector2 b = ToScreen({gridBoundsMax.x, worldY});
            DrawLineV(a, b, Fade(DARKGREEN, 0.35f));
        }
    }
    PROFILE_ZONE_END(drawWallsZone);

    PROFILE_ZONE_BEGIN(drawBulletsZone, "MainLoop::Draw / Draw Bullets");
    const std::vector<float2> bullets = manager.GetBulletPositions();
    const std::uint64_t collisionChecks = manager.GetCollisionChecksPerFrame();
    const std::uint64_t possibleCollisionChecks =
        static_cast<std::uint64_t>(bullets.size()) * static_cast<std::uint64_t>(walls.size());

    for (const float2& p : bullets) {
        DrawCircleV(ToScreen(p), 4.0f, SKYBLUE);
    }
    PROFILE_ZONE_END(drawBulletsZone);

    DrawText(TextFormat("Alive bullets: %i  Walls: %i", static_cast<int>(bullets.size()), static_cast<int>(walls.size())), 20, 50, 20, LIGHTGRAY);
    DrawText(TextFormat("Collision checks/frame: %llu", static_cast<unsigned long long>(collisionChecks)), 20, 80, 20, LIGHTGRAY);
    DrawText(TextFormat("Possible checks/frame: %llu", static_cast<unsigned long long>(possibleCollisionChecks)), 20, 110, 20, LIGHTGRAY);
    if (hasGrid) {
        DrawText(
            TextFormat(
                "Grid: %dx%d  cell: %.1f  bounds:[%.1f..%.1f]x[%.1f..%.1f]",
                gridWidth,
                gridHeight,
                gridCellSize,
                gridBoundsMin.x,
                gridBoundsMax.x,
                gridBoundsMin.y,
                gridBoundsMax.y),
            20,
            140,
            20,
            GREEN);
    }
    if (!isFiring) {
        DrawText("Press ENTER to start bullet spawning", 20, 170, 20, YELLOW);
    }
    if (simulationPaused) {
        DrawText("Simulation paused (press SPACE to resume)", 20, 200, 20, ORANGE);
    }

    PROFILE_ZONE_BEGIN(EndDrawingZone, "MainLoop::Draw / EndDrawing");
    EndDrawing();
    PROFILE_ZONE_END(EndDrawingZone);
}
}

int main() {
    InitWindow(kScreenWidth, kScreenHeight, "BulletManager assignment");
    SetTargetFPS(60);

    BulletManager manager;
    manager.ReserveBullets(1u << 17);

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
        manager.ReserveWalls(static_cast<std::size_t>(kWallCount));
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
        manager.RecalculateWallsBounds();
        manager.BuildWallSpatialGrid(kGridCellSize);
    }

    std::atomic<bool> stopWorkers = false;
    std::atomic<bool> startFiring = false;
    std::atomic<bool> simulationPaused = false;
    std::vector<std::thread> workers;

    for (int i = 0; i < 5; ++i) {
        workers.emplace_back([i, &manager, &stopWorkers, &startFiring, &simulationPaused]() {
            std::mt19937 rng(1337u + static_cast<unsigned>(i));
            std::uniform_real_distribution<float> angleDist(0.0f, 6.283185307f);
            std::uniform_real_distribution<float> yOffsetDist(-20.0f, 20.0f);
            std::uniform_real_distribution<float> speedDist(2.5f, kMaxBulletSpeed);
            // With 5 worker threads and 8s lifetime, this cadence targets ~10,000 live bullets.
            std::uniform_real_distribution<float> pauseDist(0.0035f, 0.0045f);

            while (!stopWorkers.load(std::memory_order_relaxed)) {
                if (!startFiring.load(std::memory_order_relaxed)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                if (simulationPaused.load(std::memory_order_relaxed)) {
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
        PROFILE_ZONE_SCOPED("MainLoop::Frame");

        if (IsKeyPressed(KEY_ENTER)) {
            startFiring.store(true, std::memory_order_relaxed);
        }
        if (IsKeyPressed(KEY_SPACE)) {
            simulationPaused.store(!simulationPaused.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }

        const float now = static_cast<float>(GetTime());
        if (!simulationPaused.load(std::memory_order_relaxed)) {
            manager.Update(now);
        }

        Draw(manager, startFiring.load(std::memory_order_relaxed), simulationPaused.load(std::memory_order_relaxed));
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
