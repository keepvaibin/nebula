#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FlashingCtrl;
class MapPartsRailMover;
class PartsModel;

class ChipBase : public LiveActor {
public:
    enum Type { Type_Blue = 0, Type_Yellow = 1 };

    ChipBase(const char*, s32, const char*);

    virtual ~ChipBase() override {
    }

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void makeActorAppeared() override;
    virtual void makeActorDead() override;
    virtual void control() override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void initModel(const TableIter&);
    void initSensor();
    void initShadow(const TableIter&);
    void initPlacementParam(const TableIter&);
    void deactive();
    void setGroupID(s32);
    void setHost(LiveActor*);
    void appearWait();
    void appearFlashing(s32 timer);
    bool requestGet(HitSensor*, HitSensor*);
    bool requestShow();
    bool requestHide();
    bool requestStartControl();
    bool requestEndControl();
    void exeDeactive();
    void exeWait();
    void exeControled();
    void exeFlashing();
    void exeHide();
    bool hasAirBubble() const {
        return mAirBubble != nullptr;
    }
    void exeGot();
    bool isGettable() const;
    static bool isNeedBubble(const TableIter&);

    FlashingCtrl* mFlashingCtrl;
    MapPartsRailMover* mRailMover;
    PartsModel* mAirBubble;
    const char* mChipName;
    LiveActor* mHost;
    Vec3f mClippingRange;
    s32 mGroupID;
    s32 mChipType;
    bool mIsCalcShadow;
};
