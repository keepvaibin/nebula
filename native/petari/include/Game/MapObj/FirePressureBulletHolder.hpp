#pragma once

#include "Game/NameObj/NameObj.hpp"

class FirePressureBullet;

class FirePressureBulletHolder : public NameObj {
public:
    FirePressureBulletHolder(const char* pName);

    virtual ~FirePressureBulletHolder();
    virtual void init(const TableIter& rIter);

    FirePressureBullet* callEmptyBullet();

    FirePressureBullet* mBullet[8];
};
