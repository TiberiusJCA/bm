#include "BulletManager.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kEpsilon = 1e-5f;
}

void BulletManager::AddWall(float2 a, float2 b) {
    std::scoped_lock lock(_stateMutex);
    _walls.push_back({a, b});
}

void BulletManager::Fire(float2 pos, float2 dir, float speed, float time, float lifeTime) {
    const float2 normalized = Normalize(dir);
    if ((std::abs(normalized.x) < kEpsilon && std::abs(normalized.y) < kEpsilon) || speed <= 0.0f || lifeTime <= 0.0f) {
        return;
    }

    std::scoped_lock lock(_stateMutex);
    _pendingShots.push_back({pos, normalized, speed, time, lifeTime});
}

void BulletManager::Update(float timeSeconds) {
    PROFILE_ZONE_SCOPED     ("BulletManager::Update");

    PROFILE_ZONE_BEGIN(lockWaitZone, "BulletManager::Update / lock wait");
    std::scoped_lock lock(_stateMutex);
    PROFILE_ZONE_END(lockWaitZone);

    {
        PROFILE_ZONE_SCOPED("BulletManager::Update / SpawnPendingBullets");
        SpawnPendingBullets();
    }

    if (!_hasLastUpdate) {
        _lastUpdateTime = timeSeconds;
        _hasLastUpdate = true;
    }

    const float dt = std::max(0.0f, timeSeconds - _lastUpdateTime);
    _lastUpdateTime = timeSeconds;

    {
        PROFILE_ZONE_SCOPED("BulletManager::Update / SimulateBullets");
        SimulateBullets(timeSeconds, dt);
    }

    {
        PROFILE_ZONE_SCOPED("BulletManager::Update / RemoveExpiredBullets");
        RemoveExpiredBullets(timeSeconds);
    }
}

std::vector<float2> BulletManager::GetBulletPositions() const {
    std::scoped_lock lock(_stateMutex);
    std::vector<float2> out;
    out.reserve(_bullets.size());

    for (const Bullet& bullet : _bullets) {
        if (bullet.activated) {
            out.push_back(bullet.position);
        }
    }
    return out;
}

std::vector<Wall> BulletManager::GetWalls() const {
    std::scoped_lock lock(_stateMutex);
    return _walls;
}

float2 BulletManager::Normalize(float2 v) {
    const float lenSq = v.x * v.x + v.y * v.y;
    if (lenSq <= kEpsilon) {
        return {};
    }
    const float invLen = 1.0f / std::sqrt(lenSq);
    return {v.x * invLen, v.y * invLen};
}

float BulletManager::Dot(float2 a, float2 b) {
    return a.x * b.x + a.y * b.y;
}

float2 BulletManager::Add(float2 a, float2 b) {
    return {a.x + b.x, a.y + b.y};
}

float2 BulletManager::Sub(float2 a, float2 b) {
    return {a.x - b.x, a.y - b.y};
}

float2 BulletManager::Mul(float2 v, float scalar) {
    return {v.x * scalar, v.y * scalar};
}

float2 BulletManager::Perpendicular(float2 v) {
    return {-v.y, v.x};
}

float2 BulletManager::Reflect(float2 dir, float2 normal) {
    const float2 n = Normalize(normal);
    return Sub(dir, Mul(n, 2.0f * Dot(dir, n)));
}

bool BulletManager::IntersectSegments(float2 p, float2 r, float2 q, float2 s, float& outT, float& outU) {
    const float rxs = r.x * s.y - r.y * s.x;
    if (std::abs(rxs) <= kEpsilon) {
        return false;
    }

    const float2 qMinusP = Sub(q, p);
    outT = (qMinusP.x * s.y - qMinusP.y * s.x) / rxs;
    outU = (qMinusP.x * r.y - qMinusP.y * r.x) / rxs;

    return outT >= 0.0f && outT <= 1.0f && outU >= 0.0f && outU <= 1.0f;
}

void BulletManager::SpawnPendingBullets() {
    for (const PendingFire& pending : _pendingShots) {
        _bullets.push_back({pending.pos, pending.dir, pending.speed, pending.fireTime, pending.lifeTime, false});
    }
    _pendingShots.clear();
}

void BulletManager::SimulateBullets(float timeSeconds, float deltaTime) {
    for (Bullet& bullet : _bullets) {
        if (!bullet.activated && timeSeconds >= bullet.fireTime) {
            bullet.activated = true;
        }

        if (!bullet.activated) {
            continue;
        }

        SimulateBullet(bullet, deltaTime);
    }
}

void BulletManager::RemoveExpiredBullets(float timeSeconds) {
    _bullets.erase(
        std::remove_if(
            _bullets.begin(),
            _bullets.end(),
            [timeSeconds](const Bullet& bullet) {
                return timeSeconds >= bullet.fireTime + bullet.lifeTime;
            }),
        _bullets.end());
}

void BulletManager::SimulateBullet(Bullet& bullet, float deltaTime) {
    if (deltaTime <= 0.0f) {
        return;
    }

    float remainingDistance = bullet.speed * deltaTime;
    int collisionSafetyCounter = 0;

    while (remainingDistance > kEpsilon && collisionSafetyCounter < 8) {
        const float2 displacement = Mul(bullet.direction, remainingDistance);

        std::size_t wallIndex = 0;
        float hitT = 0.0f;
        float2 hitNormal = {};
        const bool collided = TryFindClosestCollision(bullet.position, displacement, wallIndex, hitT, hitNormal);

        if (!collided) {
            bullet.position = Add(bullet.position, displacement);
            break;
        }

        const float travelBeforeHit = std::max(0.0f, hitT) * remainingDistance;
        bullet.position = Add(bullet.position, Mul(bullet.direction, travelBeforeHit));

        if (wallIndex < _walls.size()) {
            _walls.erase(_walls.begin() + static_cast<std::ptrdiff_t>(wallIndex));
        }

        bullet.direction = Normalize(Reflect(bullet.direction, hitNormal));
        remainingDistance -= travelBeforeHit;

        // Move away slightly to avoid repeatedly colliding at the same point.
        bullet.position = Add(bullet.position, Mul(bullet.direction, 1e-3f));
        remainingDistance = std::max(0.0f, remainingDistance - 1e-3f);
        ++collisionSafetyCounter;
    }
}

bool BulletManager::TryFindClosestCollision(float2 start, float2 displacement, std::size_t& wallIndex, float& hitT, float2& hitNormal) const {
    bool found = false;
    float bestT = 1.0f;

    for (std::size_t i = 0; i < _walls.size(); ++i) {
        const Wall& wall = _walls[i];
        if (!IsWallValid(wall)) {
            continue;
        }

        const float2 wallVec = Sub(wall.b, wall.a);
        float t = 0.0f;
        float u = 0.0f;

        if (!IntersectSegments(start, displacement, wall.a, wallVec, t, u)) {
            continue;
        }

        if (t < 0.0f || t > bestT) {
            continue;
        }

        bestT = t;
        wallIndex = i;
        found = true;

        float2 n = Normalize(Perpendicular(wallVec));
        if (Dot(displacement, n) > 0.0f) {
            n = Mul(n, -1.0f);
        }
        hitNormal = n;
    }

    hitT = bestT;
    return found;
}

bool BulletManager::IsWallValid(const Wall& wall) const {
    const float2 d = Sub(wall.b, wall.a);
    return (d.x * d.x + d.y * d.y) > kEpsilon;
}
