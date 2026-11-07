#pragma once

#include "Game/NameObj/NameObj.hpp"

class WaterPressureBullet;

class WaterPressureBulletHolder : public NameObj {
public:
    /// @brief Creates a new `WaterPressureBulletHolder`.
    /// @param pName A pointer to the null-terminated name of the object.
    WaterPressureBulletHolder(const char* pName);

    virtual ~WaterPressureBulletHolder();
    virtual void init(const TableIter& rIter);

    // Null when all are out: the shot is skipped (WaterPressure::shotBullet, WaterBazooka).
    WaterPressureBullet* callEmptyBullet();

    // A pool, as authored: how many bullets fly at once is part of the fights' design.
    static const s32 cBulletCount = 16;

    WaterPressureBullet* mBullet[cBulletCount];
};
