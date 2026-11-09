#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/MapObj/MapPartsFloatingForce.hpp"

class SimpleFloaterObj : public LiveActor {
public:
    SimpleFloaterObj(const char*);

    virtual void init(const TableIter&) override;

    void exeWait();
    inline bool isVelocityClose() {
        return mFloatingForce->mVelocity.length() < 0.3f;
    }

    const char* mObjName;
    MapPartsFloatingForce* mFloatingForce;
    Vec3f mEffectPos;
};
