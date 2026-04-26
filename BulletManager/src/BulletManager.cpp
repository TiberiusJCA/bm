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

    _deadBulletIndices.resize(count);
    _deadBulletCount = count;
    for (std::size_t i = 0; i < count; ++i) {
        _deadBulletIndices[i] = count - 1 - i;
    }
    _activeBulletIndices.clear();
    _activeBulletIndices.reserve(count);
    _nextActiveBulletIndices.clear();
    _nextActiveBulletIndices.reserve(count);
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

    std::scoped_lock lock(_pendingShotsMutex);
    _pendingShots.push_back({pos, normalized, speed, time, lifeTime});
}

void BulletManager::Update(float timeSeconds) {
    PROFILE_ZONE_SCOPED     ("BulletManager::Update");

    std::vector<PendingFire> pendingShots;
    {
        PROFILE_ZONE_BEGIN(pendingLockZone, "BulletManager::Update / pending lock wait");
        std::scoped_lock pendingLock(_pendingShotsMutex);
        PROFILE_ZONE_END(pendingLockZone);
        pendingShots.swap(_pendingShots);
    }

    PROFILE_ZONE_BEGIN(lockWaitZone, "BulletManager::Update / lock wait");
    std::scoped_lock lock(_stateMutex);
    PROFILE_ZONE_END(lockWaitZone);
    _collisionChecksPerFrame = 0;

    {
        PROFILE_ZONE_SCOPED("BulletManager::Update / SpawnPendingBullets");
        SpawnPendingBullets(pendingShots);
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

}

std::vector<float2> BulletManager::GetBulletPositions() const {
    std::scoped_lock lock(_stateMutex);
    std::vector<float2> out;
    out.reserve(_activeBulletIndices.size());

    for (const std::size_t index : _activeBulletIndices) {
        if (index >= _bullets.size()) {
            continue;
        }
        const Bullet& bullet = _bullets[index];
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

bool BulletManager::GetWallSpatialGridInfo(float2& boundsMin, float2& boundsMax, int& gridWidth, int& gridHeight, float& cellSize) const {
    std::scoped_lock lock(_stateMutex);
    if (!_hasWallGrid || !_hasWallsBounds) {
        return false;
    }

    boundsMin = _wallsBoundsMin;
    boundsMax = _wallsBoundsMax;
    gridWidth = _wallGridWidth;
    gridHeight = _wallGridHeight;
    cellSize = _wallGridCellSize;
    return true;
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

void BulletManager::SpawnPendingBullets(const std::vector<PendingFire>& pendingShots) {
    for (const PendingFire& pending : pendingShots) {
        if (_deadBulletCount == 0) {
            break;
        }

        --_deadBulletCount;
        const std::size_t slot = _deadBulletIndices[_deadBulletCount];
        _bullets[slot] = {pending.pos, pending.dir, pending.speed, pending.fireTime, pending.lifeTime, true, false};
        _activeBulletIndices.push_back(slot);
    }
}

void BulletManager::SimulateBullets(float timeSeconds, float deltaTime) {
    _nextActiveBulletIndices.clear();
    _nextActiveBulletIndices.reserve(_activeBulletIndices.size());
    for (const std::size_t index : _activeBulletIndices) {
        if (index >= _bullets.size()) {
            continue;
        }

        Bullet& bullet = _bullets[index];
        if (!bullet.alive) {
            continue;
        }

        if (!bullet.activated && timeSeconds >= bullet.fireTime) {
            bullet.activated = true;
        }

        if (bullet.activated) {
            SimulateBullet(bullet, deltaTime);
        }

        if (bullet.alive && timeSeconds < bullet.fireTime + bullet.lifeTime) {
            _nextActiveBulletIndices.push_back(index);
        } else {
            bullet.alive = false;
            bullet.activated = false;
            _deadBulletIndices[_deadBulletCount] = index;
            ++_deadBulletCount;
        }
    }
    _activeBulletIndices.swap(_nextActiveBulletIndices);
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

    ++_wallVisitGeneration;
    if (_wallVisitGeneration == 0) {
        std::fill(_wallVisitedStamp.begin(), _wallVisitedStamp.end(), 0);
        _wallVisitGeneration = 1;
    }

    auto testCellWalls = [&](int cellX, int cellY) {
        const std::vector<std::size_t>& cellWalls = _wallsByGridCell[GetWallGridIndex(cellX, cellY)];
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
    };

    const float2 end = Add(start, displacement);
    int cellX = GetWallCellX(start.x);
    int cellY = GetWallCellY(start.y);
    const int endCellX = GetWallCellX(end.x);
    const int endCellY = GetWallCellY(end.y);

    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const int stepX = (dx > 0.0f) ? 1 : ((dx < 0.0f) ? -1 : 0);
    const int stepY = (dy > 0.0f) ? 1 : ((dy < 0.0f) ? -1 : 0);

    auto nextBoundaryX = [&]() {
        return _wallsBoundsMin.x + ((stepX > 0 ? (cellX + 1) : cellX) * _wallGridCellSize);
    };
    auto nextBoundaryY = [&]() {
        return _wallsBoundsMin.y + ((stepY > 0 ? (cellY + 1) : cellY) * _wallGridCellSize);
    };

    const float invDx = (std::abs(dx) > kEpsilon) ? (1.0f / dx) : 0.0f;
    const float invDy = (std::abs(dy) > kEpsilon) ? (1.0f / dy) : 0.0f;

    float tMaxX = (stepX != 0) ? (nextBoundaryX() - start.x) * invDx : std::numeric_limits<float>::infinity();
    float tMaxY = (stepY != 0) ? (nextBoundaryY() - start.y) * invDy : std::numeric_limits<float>::infinity();
    float tDeltaX = (stepX != 0) ? std::abs(_wallGridCellSize * invDx) : std::numeric_limits<float>::infinity();
    float tDeltaY = (stepY != 0) ? std::abs(_wallGridCellSize * invDy) : std::numeric_limits<float>::infinity();

    testCellWalls(cellX, cellY);
    while ((cellX != endCellX || cellY != endCellY) && (tMaxX <= 1.0f || tMaxY <= 1.0f)) {
        if (tMaxX < tMaxY) {
            cellX += stepX;
            tMaxX += tDeltaX;
        } else {
            cellY += stepY;
            tMaxY += tDeltaY;
        }

        if (cellX < 0 || cellX >= _wallGridWidth || cellY < 0 || cellY >= _wallGridHeight) {
            break;
        }

        testCellWalls(cellX, cellY);
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
