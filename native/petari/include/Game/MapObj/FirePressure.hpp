#pragma once

#include "Game/MapObj/PressureBase.hpp"

class FirePressure : public PressureBase {
public:
    FirePressure(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void initBullet(const TableIter& rIter);
    virtual bool shotBullet(f32 speed);

    void calcGunPointFromCannon(Pos3f* pMtx);

    bool mIsDisableCollision;
};