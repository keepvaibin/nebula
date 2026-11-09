#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/Array.hpp"
#include <nebula_geometry.hpp>

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

    virtual ~ShadowControllerHolder() override;
    virtual void initAfterPlacement() override;
    virtual void movement() override;

    void updateController();

    MR::Vector< MR::AssignableArray< ShadowController* > > mControllers;
    MR::Vector< MR::AssignableArray< ShadowController* > > mRequested;  // this frame's, each once
};

// Where an actor's shadow falls: a drop point and direction, and the projection a line along them finds.
class ShadowController {
public:
    // Projection by collision.
    enum class CollisionCalc : u8 { Off, On, Once };
    // The drop direction from gravity. The "once" forms of the game compute every frame as well (its count test never
    // passed), so they are these.
    enum class DropGravity : u8 { None, Gravity, ShadowGravity };
    enum class DropType : u8 { Map, WaterSurface };
    enum class Visibility : u8 {
        Always,        // its own, for a shadow that comes first (MeteorStrike)
        SyncHost,      // follows the actor
        UnlessHidden,  // apart from the actor's clipping, as a table asks; a character hidden is still not there
    };

    ShadowController(LiveActor*, const char*);

    void requestCalc();
    void update();
    void updateDirection();
    ShadowDrawer* getShadowDrawer();
    LiveActor* getHost() const;
    void setShadowDrawer(ShadowDrawer*);
    void getDropPos(Vec3f*) const;
    void getDropDir(Vec3f*) const;
    f32 getDropLength() const;
    void getProjectionPos(Vec3f*) const;
    void getProjectionNormal(Vec3f*) const;
    f32 getProjectionLength() const;
    bool isProjected() const;
    bool isDraw() const;
    bool isCalcCollision() const;
    bool isCalcGravity() const;
    bool isCalcShadowGravity() const;
    void setGroupName(const char*);
    void setDropPosPtr(const Vec3f*);
    void setDropPosMtxPtr(MtxPtr, const Vec3f&);
    void setDropPosFix(const Vec3f&);
    void setDropDirPtr(const Vec3f*);
    void setProjectionPtr(const Vec3f*, const Vec3f*);
    void setDropDirFix(const Vec3f&);
    void setDropLength(f32);
    void setDropStartOffset(f32);
    void setDropTypeNormal();
    void setDropTypeSurface();
    void setProjectionFix(const Vec3f&, const Vec3f&, bool);
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
    void offVisibleSyncHostUnlessHidden();
    void validate();
    void invalidate();

    void updateProjection();

    LiveActor* mActor;
    const char* mName;
    const char* mGroupName;
    ShadowDrawer* mDrawer;
    HitSensor* mProjectedSensor;
    CollisionPartsFilterBase* mCollisionPartsFilter;
    MtxPtr mHostMtx;  // the last matrix setDropPosMtxPtr gave; the drawers orient by it
    // The drop point: *mDropPos, else mDropOffset in *mDropMtx, else mDropOffset itself.
    const Vec3f* mDropPos;
    MtxPtr mDropMtx;
    Vec3f mDropOffset;
    const Vec3f* mDropDir;  // else mDropDirection
    Vec3f mDropDirection;
    const Vec3f* mProjPos;   // else mProjectionPos
    const Vec3f* mProjNorm;  // else mProjectionNormal
    Vec3f mProjectionPos;
    Vec3f mProjectionNormal;
    f32 mStartOffset;
    f32 mDropLength;
    CollisionCalc mCollisionCalc;
    u32 mCollisionCount;  // projections since CollisionCalc::Once was set
    DropGravity mDropGravity;
    DropType mDropType;
    Visibility mVisibility;
    bool mIsProjected;
    bool mIsRequested;
    bool mIsFollowHostScale;
    bool mIsValid;
};

namespace MR {
    void addShadowController(ShadowController*);
    void requestCalcActorShadowAppear(LiveActor*);
    void requestCalcActorShadow(LiveActor*);
};  // namespace MR
