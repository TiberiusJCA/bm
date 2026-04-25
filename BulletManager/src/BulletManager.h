#pragma once

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
};

class BulletManager {
public:
    void AddWall(float2 a, float2 b);
    void Update(float timeSeconds);
    void Fire(float2 pos, float2 dir, float speed, float time, float lifeTime);

    [[nodiscard]] std::vector<float2> GetBulletPositions() const;
    [[nodiscard]] std::vector<Wall> GetWalls() const;

private:
    struct Bullet {
        float2 position;
        float2 direction;
        float speed = 0.0f;
        float fireTime = 0.0f;
        float lifeTime = 0.0f;
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

    void DrainPendingShots();
    void SimulateBullet(Bullet& bullet, float deltaTime);
    bool TryFindClosestCollision(float2 start, float2 displacement, std::size_t& wallIndex, float& hitT, float2& hitNormal) const;
    bool IsWallValid(const Wall& wall) const;

    std::vector<Bullet> bullets_;
    std::vector<Wall> walls_;
    std::vector<PendingFire> pendingShots_;

    mutable std::mutex stateMutex_;

    float lastUpdateTime_ = 0.0f;
    bool hasLastUpdate_ = false;
};
