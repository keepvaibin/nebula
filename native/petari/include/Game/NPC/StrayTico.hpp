#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class CollectTico;
class PartsModel;

/// @brief Silver Star
class StrayTico : public LiveActor {
public:
    StrayTico(const char*, CollectTico*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPush(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void initSensor();
    void setShadowDrop(const char*);
    void initShadow();
    void startGlad();
    bool requestCompleteDemo(const Vec3f& rCenter, const Vec3f& rUp, f32 angle);
    void updateWarp();
    void updateChase();
    bool isEnableSpin() const;
    bool isRescued() const;
    bool isCompleteDemoEnd() const;
    void noticeEnd();

    void exeWait();
    void exeGlad();
    void exeChase();
    void exeLauncherCamera();
    void exeCompleteDemo();

    CollectTico* mHost;
    PartsModel* mItemBubbleModel;
    Quat4f mRotateQuat;
    Vec3f mFrontVec;
    Vec3f mWarpPos;
    Vec3f mDemoCenter;
    Vec3f mDemoUp;
    Vec3f mDemoFront;
    Vec3f mDemoStartPos;
    f32 mDemoAngle;
};
