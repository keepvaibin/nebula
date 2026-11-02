#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class ActorCameraInfo;
class DummyDisplayModel;
class ModelObj;

class BreakableCage : public LiveActor {
public:
    enum CageType { CAGE_INVALID = 0, CAGE_NORMAL = 1, CAGE_LARGE = 2, CAGE_FIX = 3, CAGE_TRASH = 4 };

    BreakableCage(const char* pName);

    virtual ~BreakableCage();
    virtual void init(const JMapInfoIter& rIter);
    virtual void appear();
    virtual void kill();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    const ActorCameraInfo* getCamInfo() const {
        return mCameraInfo;
    }

    void initMapToolInfo(const JMapInfoIter& rIter);
    void initModel(const char* pName, const JMapInfoIter& rIter);
    void initBaseMtxForCage();
    bool isTypeCage() const;
    bool isAppearPowerStar() const;
    bool tryBreak();

    void exeWait();
    void exeWaitStartDemoBreak();
    void exeBreak();

    TPos3f mMtx;
    ModelObj* mBreakModel;
    CageType mCageType;
    f32 mRotationSpeed;
    DummyDisplayModel* mItemModel;
    bool mIgnoreGravity;
    bool mDelayDeadActivate;
    ActorCameraInfo* mCameraInfo;
};
