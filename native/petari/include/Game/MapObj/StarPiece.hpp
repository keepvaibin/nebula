#pragma once

#include "Game/Util/Color.hpp"

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/TriangleFilter.hpp"
#include <nebula_geometry.hpp>

class HitSensor;

struct StarPieceReceiverInfo {
public:
    inline StarPieceReceiverInfo(NameObj* pNameObj, s32 numWanted, s32 numFlying, s32 gotCount)
        : mObj(pNameObj), mNumWanted(numWanted), mNumFlying(numFlying), mGotCount(gotCount) {
    }
    NameObj* mObj;
    u32 mNumWanted;
    u32 mNumFlying;
    u32 mGotCount;
};

struct StarPieceHostInfo {
public:
    inline StarPieceHostInfo(NameObj* pNameObj, s32 numPieces, s32 numAppeared, s32 numGot)
        : mObj(pNameObj), mNumPieces(numPieces), mNumAppeared(numAppeared), mNumGot(numGot) {
    }
    bool isAppearable() {
        return mNumGot < mNumPieces && mNumAppeared < mNumPieces - mNumGot;
    }

    NameObj* mObj;
    s32 mNumPieces;
    s32 mNumAppeared;
    s32 mNumGot;
};

class StarPiece : public LiveActor {
public:
    enum groupType { groupType_noGroup, groupType_FloatingGroup, groupType_RailMoveGroup };

    StarPiece(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void appear() override;
    virtual void makeActorAppeared() override;
    virtual void kill() override;
    virtual void makeActorDead() override;
    virtual void startClipped() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void initAndSetFloatingFromGroup(const TableIter&);
    void initAndSetRailMoveFromGroup(const TableIter&);

    void setColor(s32 colorIndex);
    static s32 getNumColor();
    void appearFromGroup();
    void changeScale(f32 scale);
    void exeFloating();
    void appearHop(const Vec3f& rPosition, const Vec3f& rUp);
    void exeHop();
    void exeFall();
    bool isTouchToTarget(Vec3f* pToTarget, f32 height);
    bool isEffectLight();
    void exeToTarget();
    void exeSpinDrained();
    void exeToPlayerEnd();
    void exeThrow();
    void endThrow();
    void exeThrowFall();
    void exeFollowPlayer();
    void exeRailMove();
    void setFollowPlayerAndAppear();
    bool tryCalcGravity();
    void trySetGravityAndFront(const Vec3f& rGravity);
    // speed is unused: a throw takes the same number of steps at any distance.
    bool throwToTarget(HitSensor* pSensor, const Vec3f& rStart, const Vec3f& rGravity, f32 speed);
    bool throwToTargetCore(const Vec3f& rEnd, const Vec3f& rStart, const Vec3f& rGravity, f32 speed, bool isFall);
    void giftToTarget(StarPieceReceiverInfo* pReceiverInfo, u32 numGift, HitSensor* pSensor, const Vec3f& rPosition);
    void goToPlayer(Vec3f velocity);
    void launch(const Vec3f& rPosition, f32 randomRange, f32 speed, bool notCheckInWater, bool isForceEffectLight);
    void launch(const Vec3f& rPosition, const Vec3f& rUp, f32 randomRange, f32 speed, bool notCheckInWater, bool isForceEffectLight);
    void launch(const Vec3f& rPosition, const Vec3f& rVelocity, bool notCheckInWater, bool isForceEffectLight);
    void launchCommon(const Vec3f& rPosition, bool notCheckInWater, bool isForceEffectLight);
    // isFitScreen always sizes the effect to screenSize; otherwise it is never smaller than minScale.
    f32 calcEffectScale(f32 screenSize, f32 minScale, bool isFitScreen);
    f32 calcNearCameraScale();
    f32 calcDistToCamera();
    void emitTouchEffect(s32 port, f32 screenSize);
    void emitGettableEffect(f32 screenSize);
    void tryGotJudge();
    bool isFollowing();
    bool setFall();
    bool isOnRailMove();
    bool isFloat();
    void setReflect(const Vec3f& rNormal, const Vec3f& rHitPos);
    bool isIgnoreTriOnThrow(const Triangle* pTriangle);
    bool touchPlayer();
    void setHostInfo(StarPieceHostInfo* pHostInfo);
    bool isNerveFall() const;
    bool isNerveThrow() const;
    bool isAwayFromCamera();
    // Reflects or breaks on whatever the throw is bound to.
    bool tryHitBinded();
    void spin();
    void startSoundByWater(const char* pName, const char* pWaterName);
    void emitEffectBySize(const char* pName, const char* pSmallName);

    Vec3f mFront;

    f32 mTurnRate;
    f32 mChaseRate;
    Vec3f mThrowStart;
    Vec3f mThrowEnd;

    TriangleFilterDelegator< StarPiece >* mDelegator;
    HitSensor* mTargetSensor;
    s32 mGettableEffectStep;
    s32 mGettableDelayCounter;
    s32 mFallKillTimer;

    Color8 mColor;

    groupType mGroupType;
    StarPieceHostInfo* mHostInfo;
    StarPieceReceiverInfo* mReceiverInfo;
    s32 mNumGift;

    struct {
        bool isGoToPlayer : 1;
        // Off while a StarPieceGroup's or StarPieceMother's switch A is off.
        bool isGettable : 1;
        // Placement arg4 0: shadow on and the ground under the piece is checked for sand; 1: no shadow.
        bool isCheckGround : 1;
        bool isNoShadow : 1;

        bool isGroup : 1;
        bool InWater : 1;
        bool isForceEffectLight : 1;
        bool isLaunched : 1;
    } mFlags;
};
