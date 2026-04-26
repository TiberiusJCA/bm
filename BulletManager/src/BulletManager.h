#pragma once

#define ENABLE_PROFILING 1

#if ENABLE_PROFILING
#   include <tracy/Tracy.hpp>
#   include <tracy/TracyC.h>
#	define PROFILE_ZONE_SCOPED(N) ZoneScopedN(N)
#   define PROFILE_ZONE_BEGIN(V, N) TracyCZoneN(V, N, 1)
#   define PROFILE_ZONE_END(V) TracyCZoneEnd(V)
#else
#   define PROFILE_ZONE_SCOPED(N)
#   define PROFILE_ZONE_BEGIN(V, N)
#   define PROFILE_ZONE_END(V)
#endif

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

struct float2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Wall {
    float2 a;
    float2 b;
    bool broken = false;
};

class BulletManager {
public:
    void ReserveBullets(std::size_t count);
    void ReserveWalls(std::size_t count);
    void AddWall(float2 a, float2 b);
    void RecalculateWallsBounds();
    void BuildWallSpatialGrid(float cellSize);
    void Update(float timeSeconds);
    void Fire(float2 pos, float2 dir, float speed, float time, float lifeTime);

    [[nodiscard]] std::vector<float2> GetBulletPositions() const;
    [[nodiscard]] std::vector<Wall> GetWalls() const;
    [[nodiscard]] std::uint64_t GetCollisionChecksPerFrame() const;
    [[nodiscard]] bool GetWallSpatialGridInfo(float2& boundsMin, float2& boundsMax, int& gridWidth, int& gridHeight, float& cellSize) const;

private:
    struct Bullet {
        float2 position;
        float2 direction;
        float speed = 0.0f;
        float fireTime = 0.0f;
        float lifeTime = 0.0f;
        bool alive = false;
        bool activated = false;
    };

    struct PendingFire {
        float2 pos;
        float2 dir;
        float speed = 0.0f;
        float fireTime = 0.0f;
        float lifeTime = 0.0f;
    };

    static float2 Normalize(float2 v);
    static float Dot(float2 a, float2 b);
    static float2 Add(float2 a, float2 b);
    static float2 Sub(float2 a, float2 b);
    static float2 Mul(float2 v, float scalar);
    static float2 Reflect(float2 dir, float2 normal);
    static bool IntersectSegments(float2 p, float2 r, float2 q, float2 s, float& outT, float& outU);
    static float2 Perpendicular(float2 v);
    bool IsOutsideWallsBounds(float2 p) const;
    int GetWallCellX(float x) const;
    int GetWallCellY(float y) const;
    std::size_t GetWallGridIndex(int x, int y) const;

    void SpawnPendingBullets();
    void SimulateBullets(float timeSeconds, float deltaTime);
    void RemoveExpiredBullets(float timeSeconds);
    void SimulateBullet(Bullet& bullet, float deltaTime);
    bool TryFindClosestCollision(float2 start, float2 displacement, std::size_t& wallIndex, float& hitT, float2& hitNormal) const;
    bool IsWallValid(const Wall& wall) const;

    std::vector<Bullet> _bullets;
    std::vector<std::size_t> _deadBulletIndices;
    std::vector<Wall> _walls;
    std::vector<PendingFire> _pendingShots;

    mutable std::mutex _stateMutex;

    float2 _wallsBoundsMin = {};
    float2 _wallsBoundsMax = {};
    bool _hasWallsBounds = false;
    float _wallGridCellSize = 0.0f;
    int _wallGridWidth = 0;
    int _wallGridHeight = 0;
    bool _hasWallGrid = false;
    std::vector<std::vector<std::size_t>> _wallsByGridCell;
    mutable std::vector<std::uint32_t> _wallVisitedStamp;
    mutable std::uint32_t _wallVisitGeneration = 0;

    float _lastUpdateTime = 0.0f;
    bool _hasLastUpdate = false;
    mutable std::uint64_t _collisionChecksPerFrame = 0;
};
