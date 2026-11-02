#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class LiveActorGroup;
class SphereSelectorHandle;

enum HandlePointingPriority { Unknown_0, Unknown_1, Unknown_2 };

class SphereSelector : public LiveActor {
public:
    SphereSelector();

    virtual ~SphereSelector();
    virtual void init(const JMapInfoIter&);
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
        return MR::testDPDMenuPadDecideTrigger() && !MR::isDemoActive();
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
    LiveActor* _98;
    u32 _9C;
    LiveActor* mPointingTarget;
    s32 _A4;
    TVec2f _A8;
    bool mIsPointingInvalid;
    bool _B1;  // Something for playing sounds the first time you click on a galaxy or cancel the selection
};

class SphereSelectorFunction {
public:
    static void registerTarget(LiveActor*);
    static bool isPadButton();
    static s32 getSelectStartFrame();
    static s32 getConfirmStartCancelFrame();
    static void selectStart();
    static void selectCancel(bool);
    static void selectEnd();
    static void confirmStart();
    static void confirmCancel(bool);
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
    static void calcHandledTrans(const TVec3f&, TVec3f*);
    static void calcHandledRotateMtx(const TVec3f&, TPos3f*);
    static TVec3f& getHandleTrans();
    static f32 getHandleRotateSpeed();
    static bool isHandleHolding();
    static void registerPointingTarget(LiveActor*, HandlePointingPriority);
    static bool tryRegisterPointingTarget(LiveActor*, HandlePointingPriority);
    static bool isPointingTarget();
    static bool isPointingTarget(const LiveActor*);
    static LiveActor* getPointingTarget();
    static LiveActor* getSelectedTarget();
    static TVec3f& getSelectedActorTrans();
    static void calcOffsetPos(TVec3f*, const TVec3f&, const TVec3f&, const TVec3f&, const TVec3f&);
};
