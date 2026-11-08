#pragma once

#include "Game/MapObj/CutBushModelObj.hpp"

class PlantMember;

class PlantGroup : public LiveActor {
public:
    PlantGroup(const char*);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void makeActorAppeared();
    virtual void makeActorDead();
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void initMember(s32, const TableIter&);
    s32 placeOnCollisionFormCircle(Vec3f*, const Vec3f&, const Vec3f&, const Vec3f&);
    f32 calcBoundingSphereRadius(const Vec3f&);
    void emitHintEffect();

    PlantMember** mMembers;
    s32 mMemberCount;
    s32 mPlantType;
    bool mIsStarPiece;
    Vec3f mHintPosition;
    Vec3f mHintRotation;
    s32 mHintTimer;
    s32 mHintIndex;
};

class PlantMember : public CutBushModelObj {
public:
    PlantMember(const char* pName, const char* pModelName, bool useLight, MtxPtr pMtx) : CutBushModelObj(pName, pModelName, useLight, pMtx) {
    }

    virtual void init(const TableIter&);

    bool tryEmitHint();
    void exeWait();
    void exeHint();
    void exeShakeWeak();
    void exeShakeMiddle();
    void exeShakeStrong();
    bool generateItem(PlantGroup*);
    void initPosture();
    bool tryShake(HitSensor*);
    bool tryPush(const Vec3f&, f32, s32);
    void animControl(PlantGroup*);

    s32 mTouchType;
    bool mHasItem;
};
