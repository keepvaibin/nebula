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
    inline StarPieceReceiverInfo(NameObj* pNameObj, s32 a2, s32 a3, s32 a4) : mObj(pNameObj), _4(a2), _8(a3), mGotCount(a4) {
    }
    NameObj* mObj;
    u32 _4;
    u32 _8;
    u32 mGotCount;
};

struct StarPieceHostInfo {
public:
    inline StarPieceHostInfo(NameObj* pNameObj, s32 a2, s32 a3, s32 a4) : mObj(pNameObj), _4(a2), _8(a3), _C(a4) {
    }
    bool isAppearable() {
        return _C < _4 && _8 < _4 - _C;
    }

    NameObj* mObj;
    s32 _4;
    s32 _8;
    s32 _C;
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

    Vec3f _8C;

    f32 _98;
    f32 _9C;
    f32 _A0;
    f32 _A4;
    Vec3f _A8;
    Vec3f _B4;

    TriangleFilterDelegator< StarPiece >* mDelegator;
    HitSensor* mTargetSensor;
    s32 _C8;
    s32 mGettableDelayCounter;
    s32 mFallKillTimer;

    Color8 mColor;

    groupType mGroupType;
    StarPieceHostInfo* mHostInfo;
    StarPieceReceiverInfo* mReceiverInfo;
    /* 0XE4 */ s32 mNumGift;

    struct {
        bool isGoToPlayer : 1;
        // is turned off when part of a StarPieceGroup and switchA is off
        bool _1 : 1;
        // flags related to enabling/disabling shadows
        bool _2 : 1;
        bool _3 : 1;

        bool isGroup : 1;
        bool InWater : 1;
        // used in IsEffectLight
        bool _6 : 1;
        bool isLaunched : 1;
    } /* 0XE8 */ mFlags;
};
