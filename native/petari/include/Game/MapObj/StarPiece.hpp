#pragma once

#include "Game/Util/Color.hpp"

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/TriangleFilter.hpp"
#include <nebula_geometry.hpp>

class HitSensor;
// this function does not belong in this header, but will put here for now
namespace MR {
    void repeatDegree(f32*);
};  // namespace MR

struct StarPieceReceiverInfo {
public:
    inline StarPieceReceiverInfo(NameObj* pNameObj, s32 a2, s32 a3, s32 a4) : mObj(pNameObj), mNumWanted(a2), mNumFlying(a3), mGotCount(a4) {
    }
    NameObj* mObj;
    u32 mNumWanted;
    u32 mNumFlying;
    u32 mGotCount;
};

struct StarPieceHostInfo {
public:
    inline StarPieceHostInfo(NameObj* pNameObj, s32 a2, s32 a3, s32 a4) : mObj(pNameObj), mNumPieces(a2), mNumAppeared(a3), mNumGot(a4) {
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

    StarPiece(const char*);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void appear();
    virtual void makeActorAppeared();
    virtual void kill();
    virtual void makeActorDead();
    virtual void startClipped();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void initAndSetFloatingFromGroup(const TableIter&);
    void initAndSetRailMoveFromGroup(const TableIter&);

    void setColor(s32);
    static s32 getNumColor();
    void appearFromGroup();
    void changeScale(f32);
    void exeFloating();
    void appearHop(const Vec3f&, const Vec3f&);
    void exeHop();
    void exeFall();
    bool isTouchToTarget(Vec3f*, f32);
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
    void trySetGravityAndFront(const Vec3f&);
    bool throwToTarget(HitSensor*, const Vec3f&, const Vec3f&, f32);
    bool throwToTargetCore(const Vec3f&, const Vec3f&, const Vec3f&, f32, bool);
    void giftToTarget(StarPieceReceiverInfo*, u32, HitSensor*, const Vec3f&);
    void goToPlayer(Vec3f);
    void launch(const Vec3f&, f32, f32, bool, bool);
    void launch(const Vec3f&, const Vec3f&, f32, f32, bool, bool);
    void launch(const Vec3f&, const Vec3f&, bool, bool);
    f32 calcEffectScale(f32, f32, bool);
    f32 calcNearCameraScale();
    f32 calcDistToCamera();
    void emitTouchEffect(s32, f32);
    void emitGettableEffect(f32);
    void tryGotJudge();
    bool isFollowing();
    bool setFall();
    bool isOnRailMove();
    bool isFloat();
    void setReflect(const Vec3f&, const Vec3f&);
    bool isIgnoreTriOnThrow(const Triangle*);
    bool touchPlayer();
    void setHostInfo(StarPieceHostInfo*);

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
