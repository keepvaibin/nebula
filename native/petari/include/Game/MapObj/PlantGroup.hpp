#pragma once

#include "Game/MapObj/CutBushModelObj.hpp"

class PlantMember;

class PlantGroup : public LiveActor {
public:
    PlantGroup(const char*);

    virtual ~PlantGroup();
    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void makeActorAppeared();
    virtual void makeActorDead();
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void initMember(s32, const JMapInfoIter&);
    s32 placeOnCollisionFormCircle(TVec3f*, const TVec3f&, const TVec3f&, const TVec3f&);
    f32 calcBoundingSphereRadius(const TVec3f&);
    void emitHintEffect();

    PlantMember** mMembers;
    s32 mMemberCount;
    s32 mPlantType;
    bool mIsStarPiece;
    TVec3f mHintPosition;
    TVec3f mHintRotation;
    s32 mHintTimer;
    s32 mHintIndex;
};

class PlantMember : public CutBushModelObj {
public:
    PlantMember(const char* pName, const char* pModelName, bool useLight, MtxPtr pMtx) : CutBushModelObj(pName, pModelName, useLight, pMtx) {
    }

    virtual ~PlantMember();
    virtual void init(const JMapInfoIter&);

    bool tryEmitHint();
    void exeWait();
    void exeHint();
    void exeShakeWeak();
    void exeShakeMiddle();
    void exeShakeStrong();
    bool generateItem(PlantGroup*);
    void initPosture();
    bool tryShake(HitSensor*);
    bool tryPush(const TVec3f&, f32, s32);
    void animControl(PlantGroup*);

    f32 _90;
    f32 _94;
    s32 mTouchType;
    bool _9C;
    bool mHasItem;
};
