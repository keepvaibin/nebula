#pragma once

#include "Game/MapObj/PressureBase.hpp"

class FirePressure : public PressureBase {
public:
    FirePressure(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initBullet(const TableIter& rIter) override;
    virtual bool shotBullet(f32 speed) override;

    void calcGunPointFromCannon(Pos3f* pMtx);

    bool mIsDisableCollision;
};