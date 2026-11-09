#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class SeesawMoveNut : public MapObjActor {
public:
    SeesawMoveNut(const char*);

    virtual void init(const TableIter&) override;

    void exeHalfway();
    void exeEdge();
    void autoBackHome();
    bool isEnableStart() const;
    bool isAtEdge() const;

    f32 mSpeed;
    f32 mPrevAngularSpeed;
    s32 mBackHomeStep;
};
