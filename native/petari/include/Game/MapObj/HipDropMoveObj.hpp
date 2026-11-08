#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class HipDropMoveObj : public LiveActor {
public:
    HipDropMoveObj(const char* pName);

    virtual void init(const TableIter&);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual void moveStart() {};
    virtual void moving() {};
    virtual void moveEnd() {};
    virtual bool isEndMove() const;

    void exeWait();
    void exeMove();
    void exeDone();

    const char* mModelName;
};

class HipDropDemoMoveObj : public HipDropMoveObj {
public:
    HipDropDemoMoveObj(const char* pName);

    virtual void moveStart();
    virtual void moving();

    Mtx34f mMtx;
};
