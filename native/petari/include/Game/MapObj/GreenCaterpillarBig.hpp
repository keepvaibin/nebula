#pragma once

#include "Game/LiveActor/ModelObj.hpp"

class GreenCaterpillarBigBody : public ModelObj {
public:
    GreenCaterpillarBigBody(LiveActor*, MtxPtr);

    virtual void init(const TableIter&);
    virtual void control();
    virtual void calcAndSetBaseMtx();

    void setPosAndDirection(LiveActor*);
    void calcBodyDir(LiveActor*, Vec3f*);

    LiveActor* mCaterpillar;
    Vec3f mFrontVec;
    LodCtrl* mPlanetLOD;
};

class GreenCaterpillarBig : public LiveActor {
public:
    GreenCaterpillarBig(const char*);

    virtual void init(const TableIter&);
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();
    virtual void calcAndSetBaseMtx();

    void startWriggle();
    void exeHide();
    void exeWriggle();
    void exeRest();
    void exeEndAdjust();
    void exeEnd();
    void initBodyParts(const TableIter&);
    bool tryGenerateBodyParts();
    void fixBodyPartsOnRail();
    void leaveApple();

    GreenCaterpillarBigBody** mBodyArray;
    s32 mBodyArrayLength;
    s32 mCurBodyParts;
    s32 mRailPointNo;
    u8 mIsEnteringHole;
    u8 mIsLeavingApple;
    LodCtrl* mPlanetLOD;
};
