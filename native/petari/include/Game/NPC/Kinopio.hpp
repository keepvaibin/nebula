#pragma once

#include "Game/NPC/NPCActor.hpp"

class MapObjConnector;
class TakeOutStar;

class Kinopio : public NPCActor {
public:
    Kinopio(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void faceToVector(const Vec3f&, f32);
    void faceToPlayer(f32);
    void faceToDefault(f32);
    void setDefaultNerve();
    bool tryStartArgs();
    bool tryCancelEvent();
    bool eventFunc(u32);

    void exeReaction();
    void exeTakeOutStar();
    void exeJump();
    void exeWait();
    void exeStop();
    void exeAction();
    void exeAppear();
    void exeEscape();
    void exeStarWait();
    void exeMount();
    void exeEvent();
    void exeEventAndTalk();
    void exeEventFall();

    inline bool isPlayerNotNearKinopio();

    s32 mObjArg0;
    s32 mBehavior;
    s32 mAppearBehavior;
    s32 mGoodsIndex;
    Quat4f mConnectQuat;
    MapObjConnector* mConnector;
    TakeOutStar* mTakeOutStar;
    bool mIsSpawnPowerStarMode;
    bool mIsBgmStarted;
};
