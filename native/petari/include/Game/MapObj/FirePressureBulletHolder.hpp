#pragma once

#include "Game/NameObj/NameObj.hpp"

class FirePressureBullet;

class FirePressureBulletHolder : public NameObj {
public:
    /// @brief Creates a new `FirePressureBulletHolder`.
    /// @param pName A pointer to the null-terminated name of the object.
    FirePressureBulletHolder(const char* pName);

    virtual ~FirePressureBulletHolder();
    virtual void init(const TableIter& rIter);

    FirePressureBullet* callEmptyBullet();

    FirePressureBullet* mBullet[8];
};
