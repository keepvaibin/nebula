#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/Array.hpp"
#include <JSystem/JGeometry/TVec.hpp>

class CollisionPartsFilterBase;
class HitSensor;
class LiveActor;
class ShadowController;
class ShadowDrawer;

class ShadowControllerList {
public:
    ShadowControllerList(LiveActor*, u32);

    void addController(ShadowController*);
    u32 getControllerCount() const;
    ShadowController* getController(u32) const;
    ShadowController* getController(const char*) const;
    void resetCalcCount();
    void requestCalc();

    MR::Vector< MR::AssignableArray< ShadowController* > > mShadowList;
    LiveActor* mHost;
};

class ShadowControllerHolder : public NameObj {
public:
    ShadowControllerHolder();

    virtual ~ShadowControllerHolder();
    virtual void initAfterPlacement() override;
    virtual void movement() override;

    void updateController();

    MR::Vector< MR::AssignableArray< ShadowController* > > _C;
    MR::Vector< MR::AssignableArray< ShadowController* > > _18;
    bool _24;
    f32 mFarClip;
};

class ShadowController {
public:
    ShadowController(LiveActor*, const char*);

    void requestCalc();
    void update();
    void updateFarClipping(f32);
    void updateDirection();
    ShadowDrawer* getShadowDrawer();
    LiveActor* getHost() const;
    void setShadowDrawer(ShadowDrawer*);
    void getDropPos(TVec3f*) const;
    void getDropDir(TVec3f*) const;
    f32 getDropLength() const;
    void getProjectionPos(TVec3f*) const;
    void getProjectionNormal(TVec3f*) const;
    f32 getProjectionLength() const;
    bool isProjected() const;
    bool isDraw() const;
    bool isCalcCollision() const;
    bool isCalcGravity() const;
    bool isCalcShadowGravity() const;
    void setGroupName(const char*);
    void setDropPosPtr(const TVec3f*);
    void setDropPosMtxPtr(MtxPtr, const TVec3f&);
    void setDropPosFix(const TVec3f&);
    void setDropDirPtr(const TVec3f*);
    void setProjectionPtr(const TVec3f*, const TVec3f*);
    void setDropDirFix(const TVec3f&);
    void setDropLength(f32);
    void setDropStartOffset(f32);
    void setDropTypeNormal();
    void setDropTypeSurface();
    void setProjectionFix(const TVec3f&, const TVec3f&, bool);
    void onCalcCollision();
    void offCalcCollision();
    void onCalcCollisionOneTime();
    void onCalcDropGravity();
    void onCalcDropGravityOneTime();
    void offCalcDropGravity();
    void onCalcDropPrivateGravity();
    void onCalcDropPrivateGravityOneTime();
    void offCalcDropPrivateGravity();
    void setCollisionPartsFilter(CollisionPartsFilterBase*);
    void onFollowHostScale();
    void offFollowHostScale();
    bool isFollowHostScale() const;
    void onVisibleSyncHost();
    void offVisibleSyncHost();
    // Apart from the actor's clipping, as a table asks; a character hidden is still not there.
    void offVisibleSyncHostUnlessHidden();
    void validate();
    void invalidate();

    void updateProjection();

    inline void appendToHolder() {
        MR::getSceneObj< ShadowControllerHolder >(SceneObj_ShadowControllerHolder)->_18.push_back(this);
    }

    LiveActor* mActor;
    const char* mName;
    const char* mGroupName;
    ShadowDrawer* mDrawer;
    HitSensor* mProjectedSensor;
    CollisionPartsFilterBase* mCollisionPartsFilter;
    MtxPtr _18;
    MtxPtr _1C;
    const TVec3f* mDropPos;
    const TVec3f* mDropDir;
    const TVec3f* mProjPos;
    const TVec3f* mProjNorm;
    TVec3f _30;
    TVec3f _3C;
    TVec3f _48;
    TVec3f _54;
    u8 _60;
    u8 _61;
    u8 _62;
    u8 _63;
    u8 _64;
    u8 _65;
    u8 _66;
    u8 _67;
    f32 mStartOffset;
    f32 mDropLength;
    u8 _70;
    u8 _71;
    u8 _72;  // 1: follows the actor; 0: its own, for a shadow that comes first (MeteorStrike); or cVisibleUnlessHidden
    static const u8 cVisibleUnlessHidden = 2;
};

namespace MR {
    void addShadowController(ShadowController*);
    void requestCalcActorShadowAppear(LiveActor*);
    void requestCalcActorShadow(LiveActor*);
};  // namespace MR
