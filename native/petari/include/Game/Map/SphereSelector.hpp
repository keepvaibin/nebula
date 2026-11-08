#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include <nebula_geometry.hpp>

class LiveActorGroup;
class SphereSelectorHandle;

// Depth: the target nearest the camera wins. Last: wins only when nothing else is pointed. First: beats every other kind.
enum HandlePointingPriority { HandlePointingPriority_Depth, HandlePointingPriority_Last, HandlePointingPriority_First };

class SphereSelector : public LiveActor {
public:
    SphereSelector();

    virtual void init(const TableIter&);
    virtual void appear();
    virtual void kill();

    void registerPointingTarget(LiveActor*, HandlePointingPriority);
    void validatePointing();
    void invalidatePointing();
    void sendMsgToAllActor(u32);
    bool isMoveClickedPos() const;
    void playSelectedME();
    void playCanceledME();
    void exeSelectStart();
    void exeSelectWait();
    void exeSelectCancel();
    void exeConfirmStart();
    void exeConfirmWait();
    void exeConfirmCancel();
    void exeConfirm();

    bool isDecideTrigger() {
        return MR::testPointerMenuPadDecideTrigger() && !MR::isDemoActive();
    }

    bool isButtonAPressed() {
        return MR::isHeldJump(PLAYER_1) && !MR::isDemoActive();
    }

    void sendSelectedMsgAndSetTarget(LiveActor* pTarget) {
        if (MR::sendSimpleMsgToActor(ACTMES_SPHERE_SELECTOR_TARGET_SELECTED, pTarget)) {
            mSelectedTarget = pTarget;
        }
    }

    LiveActorGroup* mSphereGroup;
    SphereSelectorHandle* mHandle;
    LiveActor* mSelectedTarget;
    LiveActor* mNearestTarget;
    u32 mNearestDist;
    LiveActor* mPointingTarget;
    s32 mClickStep;
    Vec2f mClickPos;
    bool mIsPointingInvalid;
    bool mIsSePlayed;  // Something for playing sounds the first time you click on a galaxy or cancel the selection
};

class SphereSelectorFunction {
public:
    static void registerTarget(LiveActor*);
    static bool isPadButton();
    static s32 getSelectStartFrame();
    static s32 getConfirmStartCancelFrame();
    static void selectStart();
    static void selectCancel(bool isSePlayed);
    static void selectEnd();
    static void confirmStart();
    static void confirmCancel(bool isSePlayed);
    static void confirmed();
    static bool isSelectWait();
    static bool isConfirmWait();

    static bool isMsgSelectStart(u32);
    static bool isMsgSelectEnd(u32);
    static bool isMsgConfirmStart(u32);
    static bool isMsgConfirmCancel(u32);

    static bool isMsgConfirmed(u32);
    static bool isMsgTargetSelected(u32);
    static bool trySyncAppearMsgSelectStart(LiveActor*, u32);
    static bool trySyncKillMsgSelectStart(LiveActor*, u32);
    static void validatePointing();
    static void invalidatePointing();
    static bool isValidPointing();
    static void setHandle(SphereSelectorHandle*);
    static void calcHandledTrans(const Vec3f&, Vec3f*);
    static void calcHandledRotateMtx(const Vec3f&, Pos3f*);
    static Vec3f& getHandleTrans();
    static f32 getHandleRotateSpeed();
    static bool isHandleHolding();
    static void registerPointingTarget(LiveActor*, HandlePointingPriority);
    static bool tryRegisterPointingTarget(LiveActor*, HandlePointingPriority);
    static bool isPointingTarget();
    static bool isPointingTarget(const LiveActor*);
    static LiveActor* getPointingTarget();
    static LiveActor* getSelectedTarget();
    static Vec3f& getSelectedActorTrans();
    static void calcOffsetPos(Vec3f*, const Vec3f&, const Vec3f&, const Vec3f&, const Vec3f&);
};
