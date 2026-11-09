#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;
class DummyDisplayModel;
class ModelObj;

class BreakableCage : public LiveActor {
public:
    enum CageType { CAGE_INVALID = 0, CAGE_NORMAL = 1, CAGE_LARGE = 2, CAGE_FIX = 3, CAGE_TRASH = 4 };

    BreakableCage(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    const ActorCameraInfo* getCamInfo() const {
        return mCameraInfo;
    }

    void initMapToolInfo(const TableIter& rIter);
    void initModel(const char* pName, const TableIter& rIter);
    void initBaseMtxForCage();
    bool isTypeCage() const;
    bool isAppearPowerStar() const;
    bool tryBreak();

    void exeWait();
    void exeWaitStartDemoBreak();
    void exeBreak();

    Pos3f mMtx;
    ModelObj* mBreakModel;
    CageType mCageType;
    f32 mRotationSpeed;
    DummyDisplayModel* mItemModel;
    bool mIgnoreGravity;
    bool mDelayDeadActivate;
    ActorCameraInfo* mCameraInfo;
};
