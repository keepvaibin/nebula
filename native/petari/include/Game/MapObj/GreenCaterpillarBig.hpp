#pragma once

#include "Game/LiveActor/ModelObj.hpp"

class GreenCaterpillarBigBody : public ModelObj {
public:
    GreenCaterpillarBigBody(LiveActor*, MtxPtr);

    virtual ~GreenCaterpillarBigBody();
    virtual void init(const JMapInfoIter&);
    virtual void control();
    virtual void calcAndSetBaseMtx();

    void setPosAndDirection(LiveActor*);
    void calcBodyDir(LiveActor*, TVec3f*);

    LiveActor* mCaterpillar;
    TVec3f mFrontVec;
    LodCtrl* mPlanetLOD;
};

class GreenCaterpillarBig : public LiveActor {
public:
    GreenCaterpillarBig(const char*);

    virtual ~GreenCaterpillarBig();
    virtual void init(const JMapInfoIter&);
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
    void initBodyParts(const JMapInfoIter&);
    bool tryGenerateBodyParts();
    void fixBodyPartsOnRail();
    void leaveApple();

    GreenCaterpillarBigBody** mBodyArray;
    s32 mBodyArrayLength;
    s32 mCurBodyParts;
    s32 _98;
    u8 _9C;
    u8 _9D;
    LodCtrl* mPlanetLOD;
};
