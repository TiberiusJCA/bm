#include "BulletManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {
constexpr float kEpsilon = 1e-5f;
}

void BulletManager::ReserveBullets(std::size_t count) {
    std::scoped_lock lock(_stateMutex);
    _bullets.clear();
    _bullets.resize(count);

    _deadBulletIndices.clear();
    _deadBulletIndices.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        _deadBulletIndices.push_back(count - 1 - i);
    }
}

void BulletManager::ReserveWalls(std::size_t count) {
    std::scoped_lock lock(_stateMutex);
    _walls.reserve(count);
}

void BulletManager::AddWall(float2 a, float2 b) {
    std::scoped_lock lock(_stateMutex);
    _walls.push_back({a, b, false});
}

void BulletManager::RecalculateWallsBounds() {
    std::scoped_lock lock(_stateMutex);

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    bool hasAnyWall = false;

    for (const Wall& wall : _walls) {
        minX = std::min(minX, std::min(wall.a.x, wall.b.x));
        minY = std::min(minY, std::min(wall.a.y, wall.b.y));
        maxX = std::max(maxX, std::max(wall.a.x, wall.b.x));
        maxY = std::max(maxY, std::max(wall.a.y, wall.b.y));
        hasAnyWall = true;
    }

    _hasWallsBounds = hasAnyWall;
    if (_hasWallsBounds) {
        _wallsBoundsMin = {minX, minY};
        _wallsBoundsMax = {maxX, maxY};
    }
}

void BulletManager::BuildWallSpatialGrid(float cellSize) {
    std::scoped_lock lock(_stateMutex);

    _hasWallGrid = false;
    _wallsByGridCell.clear();
    _wallVisitedStamp.clear();
    _wallVisitGeneration = 0;

    if (!_hasWallsBounds || cellSize <= kEpsilon) {
        return;
    }

    _wallGridCellSize = cellSize;
    const float width = std::max(kEpsilon, _wallsBoundsMax.x - _wallsBoundsMin.x);
    const float height = std::max(kEpsilon, _wallsBoundsMax.y - _wallsBoundsMin.y);
    _wallGridWidth = std::max(1, static_cast<int>(std::ceil(width / _wallGridCellSize)));
    _wallGridHeight = std::max(1, static_cast<int>(std::ceil(height / _wallGridCellSize)));
    _wallsByGridCell.resize(static_cast<std::size_t>(_wallGridWidth * _wallGridHeight));

    for (std::size_t wallIndex = 0; wallIndex < _walls.size(); ++wallIndex) {
        const Wall& wall = _walls[wallIndex];
        const float minX = std::min(wall.a.x, wall.b.x);
        const float maxX = std::max(wall.a.x, wall.b.x);
        const float minY = std::min(wall.a.y, wall.b.y);
        const float maxY = std::max(wall.a.y, wall.b.y);

        const int x0 = GetWallCellX(minX);
        const int x1 = GetWallCellX(maxX);
        const int y0 = GetWallCellY(minY);
        const int y1 = GetWallCellY(maxY);

        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                _wallsByGridCell[GetWallGridIndex(x, y)].push_back(wallIndex);
            }
        }
    }

    _wallVisitedStamp.resize(_walls.size(), 0);
    _hasWallGrid = true;
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
    _collisionChecksPerFrame = 0;

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
        if (bullet.alive && bullet.activated) {
            out.push_back(bullet.position);
        }
    }
    return out;
}

std::vector<Wall> BulletManager::GetWalls() const {
    std::scoped_lock lock(_stateMutex);
    std::vector<Wall> out;
    out.reserve(_walls.size());
    for (const Wall& wall : _walls) {
        if (!wall.broken) {
            out.push_back(wall);
        }
    }
    return out;
}

std::uint64_t BulletManager::GetCollisionChecksPerFrame() const {
    std::scoped_lock lock(_stateMutex);
    return _collisionChecksPerFrame;
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
        if (_deadBulletIndices.empty()) {
            break;
        }

        const std::size_t slot = _deadBulletIndices.back();
        _deadBulletIndices.pop_back();
        _bullets[slot] = {pending.pos, pending.dir, pending.speed, pending.fireTime, pending.lifeTime, true, false};
    }
    _pendingShots.clear();
}

void BulletManager::SimulateBullets(float timeSeconds, float deltaTime) {
    for (std::size_t i = 0; i < _bullets.size(); ++i) {
        Bullet& bullet = _bullets[i];
        if (!bullet.alive) {
            continue;
        }

        if (!bullet.activated && timeSeconds >= bullet.fireTime) {
            bullet.activated = true;
        }

        if (!bullet.activated) {
            continue;
        }

        SimulateBullet(bullet, deltaTime);
        if (!bullet.alive) {
            _deadBulletIndices.push_back(i);
        }
    }
}

void BulletManager::RemoveExpiredBullets(float timeSeconds) {
    for (std::size_t i = 0; i < _bullets.size(); ++i) {
        Bullet& bullet = _bullets[i];
        if (!bullet.alive) {
            continue;
        }

        if (timeSeconds >= bullet.fireTime + bullet.lifeTime) {
            bullet.alive = false;
            bullet.activated = false;
            _deadBulletIndices.push_back(i);
        }
    }
}

void BulletManager::SimulateBullet(Bullet& bullet, float deltaTime) {
    if (deltaTime <= 0.0f) {
        return;
    }

    float remainingDistance = bullet.speed * deltaTime;
    int collisionSafetyCounter = 0;

    while (remainingDistance > kEpsilon && collisionSafetyCounter < 8) {
        if (IsOutsideWallsBounds(bullet.position)) {
            bullet.alive = false;
            bullet.activated = false;
            return;
        }

        const float2 displacement = Mul(bullet.direction, remainingDistance);

        std::size_t wallIndex = 0;
        float hitT = 0.0f;
        float2 hitNormal = {};
        const bool collided = TryFindClosestCollision(bullet.position, displacement, wallIndex, hitT, hitNormal);

        if (!collided) {
            bullet.position = Add(bullet.position, displacement);
            if (IsOutsideWallsBounds(bullet.position)) {
                bullet.alive = false;
                bullet.activated = false;
            }
            break;
        }

        const float travelBeforeHit = std::max(0.0f, hitT) * remainingDistance;
        bullet.position = Add(bullet.position, Mul(bullet.direction, travelBeforeHit));

        if (wallIndex < _walls.size()) {
            _walls[wallIndex].broken = true;
        }

        bullet.direction = Normalize(Reflect(bullet.direction, hitNormal));
        remainingDistance -= travelBeforeHit;

        // Move away slightly to avoid repeatedly colliding at the same point.
        bullet.position = Add(bullet.position, Mul(bullet.direction, 1e-3f));
        remainingDistance = std::max(0.0f, remainingDistance - 1e-3f);
        ++collisionSafetyCounter;
    }
}

bool BulletManager::IsOutsideWallsBounds(float2 p) const {
    if (!_hasWallsBounds) {
        return false;
    }

    return p.x < _wallsBoundsMin.x || p.x > _wallsBoundsMax.x || p.y < _wallsBoundsMin.y || p.y > _wallsBoundsMax.y;
}

int BulletManager::GetWallCellX(float x) const {
    if (_wallGridWidth <= 1) {
        return 0;
    }

    const float rel = (x - _wallsBoundsMin.x) / _wallGridCellSize;
    const int cell = static_cast<int>(std::floor(rel));
    return std::clamp(cell, 0, _wallGridWidth - 1);
}

int BulletManager::GetWallCellY(float y) const {
    if (_wallGridHeight <= 1) {
        return 0;
    }

    const float rel = (y - _wallsBoundsMin.y) / _wallGridCellSize;
    const int cell = static_cast<int>(std::floor(rel));
    return std::clamp(cell, 0, _wallGridHeight - 1);
}

std::size_t BulletManager::GetWallGridIndex(int x, int y) const {
    return static_cast<std::size_t>(y * _wallGridWidth + x);
}

bool BulletManager::TryFindClosestCollision(float2 start, float2 displacement, std::size_t& wallIndex, float& hitT, float2& hitNormal) const {
    bool found = false;
    float bestT = 1.0f;
    if (!_hasWallGrid || _wallGridWidth <= 0 || _wallGridHeight <= 0 || _wallsByGridCell.empty()) {
        hitT = bestT;
        return false;
    }

    const int centerX = GetWallCellX(start.x);
    const int centerY = GetWallCellY(start.y);
    ++_wallVisitGeneration;
    if (_wallVisitGeneration == 0) {
        std::fill(_wallVisitedStamp.begin(), _wallVisitedStamp.end(), 0);
        _wallVisitGeneration = 1;
    }

    for (int ny = std::max(0, centerY - 1); ny <= std::min(_wallGridHeight - 1, centerY + 1); ++ny) {
        for (int nx = std::max(0, centerX - 1); nx <= std::min(_wallGridWidth - 1, centerX + 1); ++nx) {
            const std::vector<std::size_t>& cellWalls = _wallsByGridCell[GetWallGridIndex(nx, ny)];
            for (const std::size_t i : cellWalls) {
                if (i >= _walls.size()) {
                    continue;
                }
                if (_wallVisitedStamp[i] == _wallVisitGeneration) {
                    continue;
                }
                _wallVisitedStamp[i] = _wallVisitGeneration;

                const Wall& wall = _walls[i];
                if (!IsWallValid(wall)) {
                    continue;
                }

                const float2 wallVec = Sub(wall.b, wall.a);
                float t = 0.0f;
                float u = 0.0f;
                ++_collisionChecksPerFrame;

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
        }
    }

    hitT = bestT;
    return found;
}

bool BulletManager::IsWallValid(const Wall& wall) const {
    if (wall.broken) {
        return false;
    }
    const float2 d = Sub(wall.b, wall.a);
    return (d.x * d.x + d.y * d.y) > kEpsilon;
}
