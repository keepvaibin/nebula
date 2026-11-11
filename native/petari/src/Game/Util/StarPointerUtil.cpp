#include "Game/Util/StarPointerUtil.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/MapObj/StarPieceDirector.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/StarPointerController.hpp"
#include "Game/Screen/StarPointerDirector.hpp"
#include "Game/Screen/StarPointerLayout.hpp"
#include "Game/Screen/StarPointerTarget.hpp"
#include "Game/System/GameSequenceDirector.hpp"
#include "Game/System/GameSequenceProgress.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemObjHolder.hpp"
#include "Game/System/StarPointerOnOffController.hpp"
#include "Game/System/InputRumble.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/System/NebulaInput.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcPointRadius2D_f1, "calcPointRadius2D.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcPointRadius2D_f2, "calcPointRadius2D.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcStarPointerStrokeRotateMoment_f1, "calcStarPointerStrokeRotateMoment.f1", 0.00726f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcStarPointerStrokeRotateMoment_f2, "calcStarPointerStrokeRotateMoment.f2", 0.726f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcStarPointerStrokeRotateMoment_f3, "calcStarPointerStrokeRotateMoment.f3", 0.726f);
NEBULA_TUNABLE_NAMED(f32, cNum_reportStarPointerGrabTarget_f1, "reportStarPointerGrabTarget.f1", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_reportStarPointerGrabTarget_f2, "reportStarPointerGrabTarget.f2", 4095.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_reportStarPointerGrabTarget_f3, "reportStarPointerGrabTarget.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_reportStarPointerGrabTarget_f4, "reportStarPointerGrabTarget.f4", 4095.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_reportStarPointerGrabTarget_f5, "reportStarPointerGrabTarget.f5", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_reportStarPointerGrabTarget_f6, "reportStarPointerGrabTarget.f6", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_reportStarPointerGrabTarget_f7, "reportStarPointerGrabTarget.f7", 0.5f);

namespace {

    NEBULA_TUNABLE(f32, hPointColRayLength, 99999.0f);

    class StarPointerTargetInfo {
    public:
        StarPointerTargetInfo(StarPointerTarget* pTarget, HitInfo* pHitInfo, CollisionParts* pCollisionParts)
            : mTarget(pTarget), mHitInfo(pHitInfo), mCollisionParts(pCollisionParts) {
        }

        StarPointerTarget* mTarget;
        HitInfo* mHitInfo;
        CollisionParts* mCollisionParts;
    };

    typedef bool (*StarPointerFunc1)(StarPointerTargetInfo*, const Vec3f&, const Vec2f&, f32, f32);
    typedef bool (*StarPointerFunc2)(s32);

    StarPointerDirector* getStarPointerDirector() {
        return StarPointerFunction::getStarPointerDirector();
    }

    inline StarPointerLayout* getStarPointerLayout(s32 channel) {
        return getStarPointerDirector()->getStarPointerLayout(channel);
    }

    StarPointerOnOffController* getStarPointerOnOffController() {
        return SingletonHolder< GameSystem >::get()->mSequenceDirector->mGameSequenceProgress->mStarPointerOnOffController;
    }

    inline StarPointerController* getStarPointerController(s32 channel) {
        return getStarPointerDirector()->getStarPointerController(channel);
    }

    bool always(s32 /*channel*/) {
        return true;
    }

    void onReaction(u64 touchID, s32 channel, bool enableTouch, bool disableShoot, bool singleTouch) {
        StarPointerLayout* layout = StarPointerFunction::getStarPointerDirector()->getStarPointerLayout(channel);
        if (layout != nullptr) {
            layout->mNewTouchedID = touchID;
            layout->mStartTouch = enableTouch;
            layout->mStartDisableShoot = disableShoot;
            layout->mStartSingleTouch = singleTouch;
        }
    }

    bool checkPointingTarget(StarPointerTargetInfo* pTargetInfo, const Vec3f& /*rOffset*/, const Vec2f& rPointerPos, f32 zMargin, f32 radiusMargin) {
        return pTargetInfo->mTarget->isPointing(rPointerPos, zMargin, radiusMargin);
    }

    bool checkPointingWithoutCheckZ(StarPointerTargetInfo* pTargetInfo, const Vec3f& rOffset, const Vec2f& rPointerPos, f32 /*zMargin*/,
                                    f32 radiusMargin) {
        return checkPointingTarget(pTargetInfo, rOffset, rPointerPos, ::hPointColRayLength, radiusMargin);
    }

    bool isStarPointerPointingCore(StarPointerTargetInfo* pTargetInfo, const LiveActor* pActor, s32 channel, StarPointerFunc1 targetPointCheckFunc,
                                   StarPointerFunc2 checkUpdateChannelFunc, bool enableTouch, bool disableShoot, bool singleTouch) {
        if (!getStarPointerLayout(channel)->mIsPointerValid) {
            return false;
        }

        if (!MR::isConnectedInput(channel)) {
            return false;
        }

        if (channel == PLAYER_2 && MR::isStarPointer2PTransparencyMode()) {
            return false;
        }

        if (channel == PLAYER_1 && getStarPointerOnOffController()->compareMode(StarPointerMode_1PInvalid2PValid)) {
            return false;
        }

        StarPointerController* controller = getStarPointerController(channel);
        if (!controller->isInScreen()) {
            return false;
        }

        StarPointerLayout* layout = getStarPointerLayout(channel);
        if (layout == nullptr) {
            return false;
        }

        if (!singleTouch && layout->isSingleTouch()) {
            return false;
        }

        if (singleTouch && layout->isTouch()) {
            return false;
        }

        if (!targetPointCheckFunc(pTargetInfo, controller->mWorldPos, controller->mPastInfo.mPos, controller->getViewDistZ(), layout->getRadius())) {
            return false;
        }

        onReaction(reinterpret_cast< u64 >(pActor), channel, enableTouch, disableShoot, singleTouch);

        if (!checkUpdateChannelFunc(channel)) {
            return false;
        }

        pTargetInfo->mTarget->mLastPointedChannel = channel;
        return true;
    }
    bool isPointingPaneOf(const LayoutActor*, const Ui::Pane*, const char*, s32, bool, const char*);
};  // namespace

namespace MR {

    void initStarPointerGameScene() {
        StarPointerFunction::getStarPointerDirector()->init();
    }

    void createStarPointerLayout() {
        StarPointerFunction::getStarPointerDirector()->createLayout();
    }

    void onStarPointerSceneOut() {
        ::getStarPointerLayout(PLAYER_1)->forceEndCommandStream();
        ::getStarPointerLayout(PLAYER_2)->forceEndCommandStream();
        ::getStarPointerDirector()->invalidate();
        setStarPointerModeBase();
        PlayerInputFunction::getInputRumble(PLAYER_1)->stop();
        PlayerInputFunction::getInputRumble(PLAYER_2)->stop();
    }

    void setStarPointerModeBase() {
        ::getStarPointerOnOffController()->setStateToBase(nullptr);
    }

    void setStarPointerCameraMtxAtGameScene() {
        ::getStarPointerDirector()->setGameSceneCameraMtx();
    }

    void initStarPointerTarget(LiveActor* pActor, f32 radius, const Vec3f& rOffset) {
        pActor->initActorStarPointerTarget(radius, &pActor->mPosition, pActor->getBaseMtx(), Vec3f(rOffset));
    }

    void initStarPointerTargetAtPos(LiveActor* pActor, f32 radius, const Vec3f* pPos, const Vec3f& rOffset) {
        pActor->initActorStarPointerTarget(radius, pPos, pActor->getBaseMtx(), Vec3f(rOffset));
    }

    void initStarPointerTargetAtMtx(LiveActor* pActor, f32 radius, MtxPtr pMtx, const Vec3f& rOffset) {
        pActor->initActorStarPointerTarget(radius, nullptr, pMtx, Vec3f(rOffset));
    }

    void initStarPointerTargetAtJoint(LiveActor* pActor, const char* pJointName, f32 radius, const Vec3f& rOffset) {
        MtxPtr mtx = MR::getJointMtx(pActor, pJointName);
        pActor->initActorStarPointerTarget(radius, nullptr, mtx, Vec3f(rOffset));
    }

    void addStarPointerTargetCircle(LayoutActor* pActor, const char* pLayoutName, f32 radius, const Vec2f& rPosition, const char* pPaneName) {
        pActor->mPointingTarget->addTargetCircle(pActor, pLayoutName, radius, rPosition, pPaneName);
    }

    bool isStarPointerValid(s32 channel) {
        return ::getStarPointerLayout(channel)->mIsPointerValid && MR::isConnectedInput(channel);
    }

    bool isStarPointerPointing(const LiveActor* pActor, s32 channel, bool enableTouch, const char* pStrength) {
        if (channel == PLAYER_1) {
            return MR::isStarPointerPointing1P(pActor, pStrength, enableTouch, false);
        } else {
            return MR::isStarPointerPointing2P(pActor, pStrength, enableTouch, false);
        }
    }

    bool tryStarPointerCheck(const LiveActor* pActor, bool enableTouch, const char* pStrength) {
        return MR::isStarPointerPointing1Por2P(pActor, pStrength, enableTouch, false);
    }

    bool tryStarPointerCheckWithoutRumble(LiveActor* pActor, bool enableTouch) {
        return MR::isStarPointerPointing1Por2P(pActor, nullptr, enableTouch, false);
    }

    bool isStarPointerPointingPane(const LayoutActor* pActor, const char* pLayoutName, s32 channel, bool enableTouch, const char* pStrength) {
        if (::getStarPointerLayout(channel)->mIsPointerValid) {
            return isStarPointerPointingPaneForMeterLayout(pActor, pLayoutName, channel, enableTouch, pStrength);
        }
        return false;
    }

    bool isStarPointerPointingPane(const LayoutActor* pActor, const Ui::Pane* pPane, const char* pName, s32 channel, bool enableTouch,
                                   const char* pStrength) {
        if (::getStarPointerLayout(channel)->mIsPointerValid) {
            return ::isPointingPaneOf(pActor, pPane, pName, channel, enableTouch, pStrength);
        }
        return false;
    }

    bool isStarPointerPointingPaneForMeterLayout(const LayoutActor* pActor, const char* pLayoutName, s32 channel, bool enableTouch,
                                                 const char* pStrength) {
        return ::isPointingPaneOf(pActor, nullptr, pLayoutName, channel, enableTouch, pStrength);
    }
}  // namespace MR

namespace {
// Whether the pointer is on pPane (null: the pane named pName, found only once the pointer could be on it).
bool isPointingPaneOf(const LayoutActor* pActor, const Ui::Pane* pPane, const char* pLayoutName, s32 channel, bool enableTouch,
                      const char* pStrength) {
    if (!MR::isConnectedInput(channel)) {
        return false;
    }

    if (channel == PLAYER_2 && MR::isStarPointer2PTransparencyMode()) {
        return false;
    }

    if (channel == PLAYER_1 && ::getStarPointerOnOffController()->compareMode(StarPointerMode_1PInvalid2PValid)) {
        return false;
    }

    if (!MR::isStarPointerInScreen(channel)) {
        return false;
    }

    Vec2f pos = MR::getStarPointerScreenPositionOrEdge(channel);
    const LayoutManager* pManager = pActor->getLayoutManager();
    if (pPane == nullptr) {
        pPane = pManager->findPaneByName(pLayoutName);
    }
    if (pPane != nullptr && pManager->isPointing(pPane, pos)) {
        ::onReaction(reinterpret_cast< u64 >(pActor) + reinterpret_cast< u64 >(pLayoutName), channel, enableTouch, false, false);

        if (pStrength != nullptr) {
            if (::getStarPointerLayout(channel)->isChanceToRumble()) {
                MR::tryRumblePad(pActor, pStrength, channel);
            }
        }
        return true;
    }

    return false;
}
}  // namespace

namespace MR {

    bool isStarPointerPointingTarget(const LayoutActor* pActor, const char* pLayoutName, s32 channel, bool enableTouch, const char* pStrength) {
        if (!MR::isConnectedInput(channel)) {
            return false;
        }

        if (!::getStarPointerLayout(channel)->mIsPointerValid) {
            return false;
        }

        if (channel == PLAYER_2 && isStarPointer2PTransparencyMode()) {
            return false;
        }

        if (channel == PLAYER_1 && ::getStarPointerOnOffController()->compareMode(StarPointerMode_1PInvalid2PValid)) {
            return false;
        }

        if (!isStarPointerInScreen(channel)) {
            return false;
        }

        StarPointerLayoutTarget* target = pActor->mPointingTarget->getTarget(pLayoutName);
        Vec2f pos = getStarPointerScreenPositionOrEdge(channel);
        if (target->isPointing(pos)) {
            ::onReaction(reinterpret_cast< u64 >(pActor) + reinterpret_cast< u64 >(pLayoutName), channel, enableTouch, false, false);

            if (pStrength != nullptr) {
                if (::getStarPointerLayout(channel)->isChanceToRumble()) {
                    MR::tryRumblePad(pActor, pStrength, channel);
                }
            }
            return true;
        }

        return false;
    }

    bool isStarPointerPointing1P(const LiveActor* pActor, const char* pStrength, bool enableTouch, bool disableShoot) {
        StarPointerTargetInfo info(pActor->mStarPointerTarget, nullptr, nullptr);

        if (::isStarPointerPointingCore(&info, pActor, PLAYER_1, ::checkPointingTarget, ::always, enableTouch, disableShoot, false)) {
            if (pStrength != nullptr && ::getStarPointerLayout(PLAYER_1)->isChanceToRumble()) {
                MR::tryRumblePad(pActor, pStrength, PLAYER_1);
            }
            return true;
        }
        return false;
    }

    bool isStarPointerPointing1PWithoutCheckZ(const LiveActor* pActor, const char* pStrength, bool enableTouch, bool disableShoot) {
        StarPointerTargetInfo info(pActor->mStarPointerTarget, nullptr, nullptr);

        if (::isStarPointerPointingCore(&info, pActor, PLAYER_1, ::checkPointingWithoutCheckZ, ::always, enableTouch, disableShoot, false)) {
            if (pStrength != nullptr && ::getStarPointerLayout(PLAYER_1)->isChanceToRumble()) {
                MR::tryRumblePad(pActor, pStrength, PLAYER_1);
            }
            return true;
        }
        return false;
    }

    bool isStarPointerPointing2P(const LiveActor* pActor, const char* pStrength, bool enableTouch, bool disableShoot) {
        StarPointerTargetInfo info(pActor->mStarPointerTarget, nullptr, nullptr);

        if (::isStarPointerPointingCore(&info, pActor, PLAYER_2, ::checkPointingTarget, ::always, enableTouch, disableShoot, false)) {
            if (pStrength != nullptr && ::getStarPointerLayout(PLAYER_2)->isChanceToRumble()) {
                MR::tryRumblePad(pActor, pStrength, PLAYER_2);
            }
            return true;
        }
        return false;
    }

    bool isStarPointerPointing2POnPressButton(const LiveActor* pActor, const char* pStrength, bool enableTouch, bool disableShoot) {
        StarPointerTargetInfo info(pActor->mStarPointerTarget, nullptr, nullptr);

        if (::isStarPointerPointingCore(&info, pActor, PLAYER_2, ::checkPointingTarget, MR::isHeldJump, enableTouch, disableShoot, false)) {
            if (pStrength != nullptr && ::getStarPointerLayout(PLAYER_2)->isChanceToRumble()) {
                MR::tryRumblePad(pActor, pStrength, PLAYER_2);
            }
            return true;
        }
        return false;
    }

    bool isStarPointerPointing2POnTriggerButton(const LiveActor* pActor, const char* pStrength, bool enableTouch, bool disableShoot) {
        StarPointerTargetInfo info(pActor->mStarPointerTarget, nullptr, nullptr);

        if (::isStarPointerPointingCore(&info, pActor, PLAYER_2, ::checkPointingTarget, MR::isPressedJump, enableTouch, disableShoot,
                                        false)) {
            if (pStrength != nullptr) {
                MR::tryRumblePad(pActor, pStrength, PLAYER_2);
            }
            return true;
        }

        return false;
    }

    bool isStarPointerPointingFileSelect(const LiveActor* pActor) {
        StarPointerTargetInfo info(pActor->mStarPointerTarget, nullptr, nullptr);
        return ::isStarPointerPointingCore(&info, pActor, PLAYER_1, ::checkPointingWithoutCheckZ, ::always, true, false, false);
    }

    bool isStarPointerPointing1Por2P(const LiveActor* pActor, const char* pStrength, bool enableTouch, bool disableShoot) {
        StarPointerTargetInfo info(pActor->mStarPointerTarget, nullptr, nullptr);

        if (::isStarPointerPointingCore(&info, pActor, PLAYER_1, ::checkPointingTarget, ::always, enableTouch, disableShoot, false)) {
            if (pStrength != nullptr && ::getStarPointerLayout(PLAYER_1)->isChanceToRumble()) {
                MR::tryRumblePad(pActor, pStrength, PLAYER_1);
            }
            return true;

        } else if (::isStarPointerPointingCore(&info, pActor, PLAYER_2, ::checkPointingTarget, ::always, enableTouch, disableShoot, false)) {
            if (pStrength != nullptr && ::getStarPointerLayout(PLAYER_2)->isChanceToRumble()) {
                MR::tryRumblePad(pActor, pStrength, PLAYER_2);
            }
            return true;
        }
        return false;
    }

    bool requestBlueStarGuidance() {
        if (MR::isInAreaObj("BlueStarGuidanceCube", *MR::getPlayerPos()) && !MR::isSystemTalking()) {
            return ::getStarPointerDirector()->mGuidance->request1PGuidance("PointerGuidance_BlueStar", true);
        }

        return false;
    }

    bool requestTicoSeedGuidance(s32 arg) {
        if (MR::isDemoActive()) {
            return false;
        }

        AreaObj* area = MR::getAreaObj("TicoSeedGuidanceCube", *MR::getPlayerPos());
        if (area != nullptr && arg == MR::getAreaObjArg(area, 0)) {
            return ::getStarPointerDirector()->mGuidance->request1PGuidance("PointerGuidance_TicoFat", true);
        }

        return false;
    }

    bool requestTicoSeedGuidanceForce() {
        if (MR::isDemoActive()) {
            return false;
        }

        return ::getStarPointerDirector()->mGuidance->request1PGuidance("PointerGuidance_TicoFat", true);
    }

    bool requestBigBubbleGuidance() {
        return ::getStarPointerDirector()->mGuidance->request1PGuidance("PointerGuidance_BigBubble", true);
    }

    bool requestMarioLauncherGuidance() {
        return ::getStarPointerDirector()->mGuidance->request1PGuidance("PointerGuidance_MarioLauncher", true);
    }

    bool requestFileSelectGuidance() {
        return ::getStarPointerDirector()->mGuidance->request1PGuidance("System_FileSelect008", true);
    }

    bool requestFileSelectCopyGuidance() {
        return ::getStarPointerDirector()->mGuidance->request1PGuidance("System_FileSelect002", false);
    }

    bool requestStarPieceLectureGuidance() {
        return ::getStarPointerDirector()->mGuidance->request1PGuidance("PointerGuidance_StarPieceLecture", false);
    }

    bool requestPointerGuidanceNoInformation() {
        return ::getStarPointerDirector()->mGuidance->request1PGuidance(nullptr, true);
    }

    bool isExistStarPointerGuidance() {
        return ::getStarPointerDirector()->mGuidance->isExistGuidanceOrFrame();
    }

    bool isExistStarPointerGuidanceFrame1P() {
        return ::getStarPointerDirector()->mGuidance->isExistFrame1P();
    }

    void activeStarPointerGuidance() {
        if (::getStarPointerDirector()->mGuidance != nullptr) {
            ::getStarPointerDirector()->mGuidance->active();
        }
    }

    void deactiveStarPointerGuidance() {
        if (::getStarPointerDirector()->mGuidance != nullptr) {
            ::getStarPointerDirector()->mGuidance->deactive();
        }
    }

    bool isActiveStarPointerGuidance() {
        return ::getStarPointerDirector()->mGuidance != nullptr && ::getStarPointerDirector()->mGuidance->mIsActive;
    }

    void tryShowTimeoutedStarPointerGuidance() {
        if (::getStarPointerDirector()->mGuidance != nullptr) {
            ::getStarPointerDirector()->mGuidance->tryResetTimeout();
        }
    }

    bool isExistStarPointerTarget(const LiveActor* pActor) {
        return pActor->mStarPointerTarget != nullptr;
    }

    void setStarPointerTargetRadius3d(LiveActor* pActor, f32 radius) {
        pActor->mStarPointerTarget->mRadius3d = radius;
    }

    bool isStarPointerPointing(const Vec3f& rPos, f32 radius, s32 channel, bool enableTouch) {
        StarPointerTarget target(radius, &rPos, nullptr, Vec3f(0.0f, 0.0f, 0.0f));

        StarPointerController* controller = ::getStarPointerController(channel);

        if (!::getStarPointerLayout(channel)->mIsPointerValid) {
            return false;
        }

        if (!controller->isInScreen()) {
            return false;
        }

        StarPointerLayout* layout = ::getStarPointerLayout(channel);
        if (layout == nullptr) {
            return false;
        }

        if (target.isPointing(controller->mPastInfo.mPos, controller->getViewDistZ(), layout->getRadius())) {
            ::onReaction(reinterpret_cast< u64 >(&rPos), channel, enableTouch, false, false);
            return true;
        }

        return false;
    }

    MtxPtr getStarPointerViewMtx() {
        return ::getStarPointerDirector()->mTransHolder->mViewMtx;
    }

    Mtx44Ptr getStarPointerProjMtx() {
        return ::getStarPointerDirector()->mTransHolder->mProjMtx;
    }

    // The 2D position menus and layouts test is this frame's, where the pointer is drawn (StarPointerLayout).
    Vec2f* getStarPointerScreenPosition(s32 channel) {
        return &::getStarPointerController(channel)->mInfo.mPos;
    }

    Vec2f getStarPointerScreenPositionOrEdge(s32 channel) {
        Vec2f pos(::getStarPointerController(channel)->mInfo.mPos);
        StarPointerFunction::forceInsideScreenEdge(&pos);
        return pos;
    }

    f32 getStarPointerScreenSpeed(s32 channel) {
        return ::getStarPointerController(channel)->mScreenSpeed;
    }

    bool isStarPointerInScreen(s32 channel) {
        return ::getStarPointerController(channel)->isInScreen();
    }

    bool isStarPointerInScreenAnyPort(s32* pInScreenChannel) {
        for (s32 channel = 0; channel < StarPointerFunction::getNumStarPointer(); channel++) {
            if (isStarPointerInScreen(channel)) {
                if (pInScreenChannel != nullptr) {
                    *pInScreenChannel = channel;
                }
                return true;
            }
        }
        if (pInScreenChannel != nullptr) {
            *pInScreenChannel = -1;
        }
        return false;
    }

    s32* getStarPointerLastPointedPort(const LiveActor* pActor) {
        return &pActor->mStarPointerTarget->mLastPointedChannel;
    }

    Vec2f* getStarPointerScreenVelocity(s32 channel) {
        return &::getStarPointerController(channel)->mScreenVel;
    }

    void getStarPointerWorldVelocityDirection(Vec3f* pVel, s32 channel) {
        pVel->set(::getStarPointerController(channel)->mWorldVel);
    }

    Vec3f* getStarPointerWorldPosUsingDepth(s32 channel) {
        return &::getStarPointerController(channel)->mWorldPos;
    }

    void calcStarPointerWorldPointingPos(Vec3f* pPos, const Vec3f& rPos, s32 channel) {
        f32 camZ = MR::calcCameraDistanceZ(rPos);
        MR::calcWorldPositionFromScreen(pPos, ::getStarPointerController(channel)->mPastInfo.mPos, camZ);
    }

    void calcStarPointerWorldPointingPosInsideEdge(Vec3f* pPos, const Vec3f& rPos, s32 channel) {
        f32 camZ = MR::calcCameraDistanceZ(rPos);
        MR::calcWorldPositionFromScreen(pPos, getStarPointerScreenPositionOrEdge(channel), camZ);
    }

    bool calcStarPointerPosOnPlane(Vec3f* pPos, const Vec3f& rPlaneBasePos, const Vec3f& rPlaneNorm, s32 channel, bool clampEdge) {
        Vec2f screenPos;
        if (clampEdge) {
            screenPos = getStarPointerScreenPositionOrEdge(channel);
        } else {
            screenPos.set(*getStarPointerScreenPosition(channel));
        }

        Vec3f camPos = MR::getCamPos();
        Vec3f pointRay;
        MR::calcWorldPositionFromScreen(&pointRay, screenPos, -1.0f);
        pointRay.sub(camPos);
        MR::normalize(&pointRay);

        if (MR::checkHitSemilinePlane(pPos, camPos, pointRay, rPlaneBasePos, rPlaneNorm)) {
            return true;
        }

        Vec3f camNorm = -MR::getCamZdir();
        Vec3f forceIntersectPos;
        MR::checkHitSemilinePlane(&forceIntersectPos, camPos, pointRay, rPlaneBasePos, camNorm);

        Vec3f forceNorm;
        Vec3f checkDir = rPlaneBasePos;
        checkDir.sub(forceIntersectPos);

        if (checkDir.dot(rPlaneNorm) < 0.0f) {
            forceNorm = -rPlaneNorm;
        } else {
            forceNorm = rPlaneNorm;
        }

        MR::checkHitSemilinePlane(pPos, forceIntersectPos, forceNorm, rPlaneBasePos, rPlaneNorm);

        return true;
    }

    bool calcStarPointerWorldVelocityDirectionOnPlane(Vec3f* pDir, const Vec3f& rPlaneBasePos, const Vec3f& rPlaneNorm, s32 channel) {
        StarPointerController* controller = ::getStarPointerController(channel);
        Vec3f camPos = MR::getCamPos();
        const Vec2f& posA = controller->mPastInfo.mPos;
        const Vec2f& posB = posA.subInline(controller->mScreenVel);
        Vec3f toPointA, toPointB;
        MR::calcWorldPositionFromScreen(&toPointA, posA, -1.0f);
        MR::calcWorldPositionFromScreen(&toPointB, posB, -1.0f);
        toPointA.sub(camPos);
        toPointB.sub(camPos);
        MR::normalize(&toPointA);
        MR::normalize(&toPointB);

        f32 projA = toPointA.dot(rPlaneNorm);
        if (MR::isNearZero(projA)) {
            return false;
        }

        f32 projB = toPointB.dot(rPlaneNorm);
        if (MR::isNearZero(projB)) {
            return false;
        }

        Vec3f camDiff = rPlaneBasePos;
        camDiff.sub(camPos);
        f32 projCam = rPlaneNorm.dot(camDiff);

        f32 distA = projCam / projA;
        f32 distB = projCam / projB;

        Vec3f planePosA, planePosB;
        planePosA.set(camPos);
        planePosA.add(toPointA * distA);
        planePosB.set(camPos);
        planePosB.add(toPointB * distB);
        pDir->set(planePosA);
        pDir->sub(planePosB);
        MR::normalizeOrZero(pDir);
        return true;
    }

    f32 calcPointRadius2D(const Vec3f& rPosition, f32 radius) {
        f32 fovyRad = MR::toRadian(MR::getRenderFovy());
        f32 tan = MR::tan(fovyRad * cNum_calcPointRadius2D_f1);
        f32 focalDist = (static_cast< s32 >(NebulaFrameClock::cScreenLines) * cNum_calcPointRadius2D_f2) / tan;

        Vec3f viewPos;
        MR::getCameraViewMtx().mult(rPosition, viewPos);

        return radius * focalDist / -viewPos.z;
    }

    bool calcStarPointerStrokeRotateMoment(Vec3f* pMoment, const Vec3f& rPosition, f32 radius, s32 channel) {
        if (!isStarPointerInScreen(channel)) {
            pMoment->zero();
            return false;
        }

        Vec2f screenVel = ::getStarPointerController(channel)->mScreenVel;

        f32 rad2D = MR::calcPointRadius2D(rPosition, radius);
        screenVel.scale(1.0f / rad2D);

        f32 speed = screenVel.length();
        if (speed < cNum_calcStarPointerStrokeRotateMoment_f1) {
            pMoment->zero();
            return false;
        }
        if (speed > cNum_calcStarPointerStrokeRotateMoment_f2) {
            screenVel.scale(cNum_calcStarPointerStrokeRotateMoment_f3 / speed);
        }

        Vec2f screenPos;
        screenPos.set(::getStarPointerController(channel)->mPastInfo.mPos);

        Vec2f pos2D;
        MR::calcScreenPosition(&pos2D, rPosition);

        f32 dist;
        Vec2f dir;
        MR::separateScalarAndDirection(&dist, &dir, screenPos - pos2D);

        f32 ratio = dist / rad2D;
        if (ratio > 1.0f) {
            pMoment->zero();
            return false;
        }
        ratio = MR::clamp(ratio, 0.0f, 1.0f);

        Vec3f moment;
        moment.set< f32 >(screenVel.y, screenVel.x, 0.0f);

        Vec3f stroke;
        stroke.x = dir.x * ratio;
        stroke.y = -dir.y * ratio;
        stroke.z = MR::sqrt(1.0f - ratio * ratio);

        Quat4f rot;
        Vec3f front;
        front.x = 0.0f;
        front.y = 0.0f;
        front.z = 1.0f;
        rot.setRotate(front, stroke);
        rot.rotate(moment);

        MR::getCameraInvViewMtx().mult33(moment, *pMoment);
        return true;
    }

    bool calcStarPointerScreenDistanceToTarget(const LiveActor* pActor, f32* pDistance, s32 channel) {
        Vec2f screenPos;
        if (!pActor->mStarPointerTarget->calcScreenPosition(&screenPos)) {
            return false;
        }

        StarPointerController* controller = ::getStarPointerController(channel);
        if (!::getStarPointerLayout(channel)->mIsPointerValid) {
            return false;
        }

        if (!controller->isInScreen()) {
            return false;
        }

        Vec2f targetPos(controller->mPastInfo.mPos);
        screenPos -= targetPos;
        *pDistance = screenPos.length();
        return true;
    }

    bool tryStartStarPointerCommandStream(const LiveActor* pActor, const Vec3f* pPos, s32 channel, bool b) {
        return ::getStarPointerLayout(channel)->startCommandStream(pActor, pPos, b);
    }

    bool tryEndStarPointerCommandStream(const LiveActor* pActor, s32 channel) {
        StarPointerLayout* layout = ::getStarPointerLayout(channel);
        if (layout->isCommandStream(pActor)) {
            layout->endCommandStream(pActor);
            return true;
        }
        return false;
    }

    bool isStarPointerCommandStream(const LiveActor* pActor, s32 channel) {
        return ::getStarPointerLayout(channel)->isCommandStream(pActor);
    }

    void startStarPointerModeTitle(void* pRequester) {
        ::getStarPointerOnOffController()->setStateToTitle(pRequester);
    }

    void startStarPointerModeFileSelect(void* pRequester) {
        ::getStarPointerOnOffController()->setStateToFileSelect(pRequester);
    }

    void startStarPointerModeGame(void* pRequester) {
        ::getStarPointerOnOffController()->incModeCounter(pRequester, StarPointerMode_Game);
    }

    void startStarPointerModeDemo(void* pRequester) {
        ::getStarPointerOnOffController()->incModeCounter(pRequester, StarPointerMode_Demo);
    }

    void startStarPointerModeDemoWithStarPointer(void* pRequester) {
        ::getStarPointerOnOffController()->incModeCounter(pRequester, StarPointerMode_DemoWithStarPointer);
    }

    void startStarPointerModeDemoWithHandPointerFinger(void* pRequester) {
        ::getStarPointerOnOffController()->incModeCounter(pRequester, StarPointerMode_DemoWithHandPointerFinger);
    }

    void startStarPointerModeDemoMarioDeath(void* pRequester) {
        ::getStarPointerOnOffController()->incModeCounter(pRequester, StarPointerMode_DemoMarioDeath);
    }

    void startStarPointerModeMarioLauncher(void* pRequester) {
        ::getStarPointerOnOffController()->incModeCounter(pRequester, StarPointerMode_MarioLauncher);
    }

    void startStarPointerModeChooseYesNo(void* pRequester) {
        ::getStarPointerOnOffController()->incModeCounter(pRequester, StarPointerMode_ChooseYesNo);
    }

    void startStarPointerModePauseMenu(void* pRequester) {
        ::getStarPointerOnOffController()->incModeCounter(pRequester, StarPointerMode_PauseMenu);
    }

    void startStarPointerModeScenarioSelectScene(void* pRequester) {
        ::getStarPointerOnOffController()->incModeCounter(pRequester, StarPointerMode_ScenarioSelectScene);
    }

    void startStarPointerModeBlueStar(void* pRequester) {
        ::getStarPointerOnOffController()->incModeCounter(pRequester, StarPointerMode_BlueStar);
    }

    void startStarPointerModePowerStarGetDemo(void* pRequester) {
        ::getStarPointerOnOffController()->incModeCounter(pRequester, StarPointerMode_PowerStarGetDemo);
    }

    void startStarPointerModeStarPieceTarget(void* pRequester) {
        ::getStarPointerOnOffController()->incModeCounter(pRequester, StarPointerMode_StarPieceTarget);
    }

    void startStarPointerModeSphereSelectorFinger(void* pRequester) {
        ::getStarPointerOnOffController()->incModeCounter(pRequester, StarPointerMode_SphereSelectorFinger);
    }

    void startStarPointerModeSphereSelectorOnReaction(void* pRequester) {
        ::getStarPointerOnOffController()->incModeCounter(pRequester, StarPointerMode_SphereSelectorOnReaction);
    }

    void startStarPointerModeEnding(void* pRequester) {
        ::getStarPointerOnOffController()->incModeCounter(pRequester, StarPointerMode_Base);
    }

    void startStarPointerModeCommandStream(void* pRequester) {
        ::getStarPointerOnOffController()->incModeCounter(pRequester, StarPointerMode_CommandStream);
    }

    void startStarPointerMode1PInvalid2PValid(void* pRequester) {
        ::getStarPointerOnOffController()->incModeCounter(pRequester, StarPointerMode_1PInvalid2PValid);
    }

    void endStarPointerMode(void* pRequester) {
        ::getStarPointerOnOffController()->popState(pRequester);
    }

    void requestStarPointerModeErrorWindow(void* pRequester) {
        ::getStarPointerOnOffController()->requestMode(pRequester, StarPointerMode_ErrorWindow);
    }

    void requestStarPointerModeSaveLoad(void* pRequester) {
        ::getStarPointerOnOffController()->requestMode(pRequester, StarPointerMode_SaveLoad);
    }

    void updateStarPointerModeWhileSceneHeld() {
        ::getStarPointerOnOffController()->update();
    }

    void requestStarPointerModePictureBook(void* pRequester) {
        ::getStarPointerOnOffController()->requestMode(pRequester, StarPointerMode_PictureBook);
    }

    void requestStarPointerModePauseMenu(void* pRequester) {
        ::getStarPointerOnOffController()->requestMode(pRequester, StarPointerMode_PauseMenu);
    }

    void requestStarPointerModeBlueStarReady(void* pRequester) {
        ::getStarPointerOnOffController()->requestMode(pRequester, StarPointerMode_BlueStarReady);
    }

    void reportStarPointerGrabTarget(const LiveActor* pActor) {
        const StarPointerTarget* pTarget = pActor->mStarPointerTarget;
        if (pTarget == nullptr || NebulaHost::option(NEBULA_OPTION_TOUCH_POINTER) == 0) {
            return;
        }
        Vec2f position;
        f32 radius;
        if (!pTarget->calcScreenCircle(&position, &radius)) {
            return;
        }
        const f32 height = static_cast< f32 >(MR::getScreenHeight());
        const f32 across = radius / height * cNum_reportStarPointerGrabTarget_f1;
        const u32 x = static_cast< u32 >(position.x / MR::getScreenWidth() * cNum_reportStarPointerGrabTarget_f2 + cNum_reportStarPointerGrabTarget_f3);
        const u32 y = static_cast< u32 >(position.y / height * cNum_reportStarPointerGrabTarget_f4 + cNum_reportStarPointerGrabTarget_f5);
        const u32 r = across >= cNum_reportStarPointerGrabTarget_f6 ? 255u : static_cast< u32 >(across + cNum_reportStarPointerGrabTarget_f7);
        NebulaHost::gameContext(NEBULA_CONTEXT_TOUCH_TARGET, static_cast< s32 >(x | (y << 12) | (r << 24)));
    }

    void requestStarPointerModeBigBubble(void* pRequester, const Vec3f& rPosition) {
        ::getStarPointerOnOffController()->requestMode(pRequester, StarPointerMode_BigBubble);
        ::getStarPointerDirector()->mNozzleAimPos.set(rPosition);
    }

    bool isStarPointerModeBlueStarReady() {
        return ::getStarPointerOnOffController()->compareMode(StarPointerMode_BlueStarReady);
    }

    bool isStarPointerModeStarPieceTarget() {
        return ::getStarPointerOnOffController()->compareMode(StarPointerMode_StarPieceTarget);
    }

    bool isStarPointerModeMarioLauncher() {
        return ::getStarPointerOnOffController()->isMode(StarPointerMode_MarioLauncher);
    }

    bool isStarPointerModeHomeButton() {
        return ::getStarPointerOnOffController()->isMode(StarPointerMode_HomeButton);
    }

    bool isStarPointerModeErrorWindow() {
        return ::getStarPointerOnOffController()->isMode(StarPointerMode_ErrorWindow);
    }

    void enableStarPointerShootStarPiece() {
        StarPointerDirector* director = ::getStarPointerDirector();
        director->mIsAllowP1StarPieceShot = true;
        director->mIsAllowP2StarPieceShot = true;
    }

    void disableStarPointerShootStarPiece() {
        StarPointerDirector* director = ::getStarPointerDirector();
        director->mIsAllowP1StarPieceShot = false;
        director->mIsAllowP2StarPieceShot = false;
    }

    bool isEnableStarPointerShootStarPiece(s32 channel) {
        StarPointerDirector* director = ::getStarPointerDirector();
        if (channel == PLAYER_1) {
            return director->mIsAllowP1StarPieceShot;
        } else {
            return director->isEnableStarPointerShootStarPiece();
        }
    }

    bool isStarPointer2PTransparencyMode() {
        return ::getStarPointerOnOffController()->compareMode(StarPointerMode_FileSelect) ||
               ::getStarPointerOnOffController()->compareMode(StarPointerMode_ScenarioSelectScene) ||
               ::getStarPointerOnOffController()->compareMode(StarPointerMode_PauseMenu) ||
               ::getStarPointerOnOffController()->compareMode(StarPointerMode_ChooseYesNo) ||
               ::getStarPointerOnOffController()->compareMode(StarPointerMode_PictureBook) ||
               ::getStarPointerOnOffController()->compareMode(StarPointerMode_SphereSelectorOnReaction) ||
               ::getStarPointerOnOffController()->compareMode(StarPointerMode_SphereSelectorFinger) ||
               ::getStarPointerOnOffController()->compareMode(StarPointerMode_SaveLoad) ||
               ::getStarPointerOnOffController()->compareMode(StarPointerMode_StarPieceTarget) ||
               ::getStarPointerOnOffController()->compareMode(StarPointerMode_MarioLauncher);
    }

    bool isStarPointer1PInvalid2PValidMode() {
        return ::getStarPointerOnOffController()->mMode == StarPointerMode_1PInvalid2PValid;
    }

    void setStarPointerDrawSyncToken() {
        StarPointerDirector* director = ::getStarPointerDirector();
        if (director->getStarPointerLayout(PLAYER_1)->mIsPointerValid || director->getStarPointerLayout(PLAYER_2)->mIsPointerValid) {
            ::getStarPointerDirector()->mPeekZ->setDrawSyncToken();
        }
    }

};  // namespace MR
