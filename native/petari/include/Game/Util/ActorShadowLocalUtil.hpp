#pragma once


class TableIter;
class LiveActor;
class ShadowController;
class ShadowDrawer;
class ShadowSurfaceBox;
class ShadowSurfaceCircle;
class ShadowSurfaceOval;
class ShadowVolumeBox;
class ShadowVolumeCylinder;
class ShadowVolumeDrawer;
class ShadowVolumeFlatModel;
class ShadowVolumeLine;
class ShadowVolumeOval;
class ShadowVolumeOvalPole;
class ShadowVolumeSphere;

namespace ActorShadow {
    u32 getShadowControllerCount(const LiveActor*);
    ShadowController* getShadowController(const LiveActor*, u32);
    ShadowController* getShadowController(const LiveActor*, const char*);
    bool isExistShadowController(const LiveActor*, const char*);
    ShadowController* createShadowControllerVolumeParam(LiveActor*, const char*);
    ShadowController* createShadowControllerSuefaceParam(LiveActor*, const char*);
    ShadowSurfaceOval* getShadowSurfaceOval(const LiveActor*, const char*);
    ShadowVolumeSphere* getShadowVolumeSphere(const LiveActor*, const char*);
    ShadowVolumeCylinder* getShadowVolumeCylinder(const LiveActor*, const char*);
    ShadowVolumeBox* getShadowVolumeBox(const LiveActor*, const char*);
    ShadowVolumeDrawer* getShadowVolumeDrawer(const LiveActor*, const char*);
    bool getJointNameFromCSV(const char**, const TableIter&);
    s32 getShadowTypeFromCSV(const TableIter&);
    void setUpShadowControlBaseMtxFromCSV(ShadowController*, LiveActor*, const TableIter&);
    void setUpShadowControlIsFollowScaleFromCSV(ShadowController*, const TableIter&);
    void setUpShadowControlIsSyncShowFromCSV(ShadowController*, const TableIter&);
    void setUpShadowControlCalcCollisionFromCSV(ShadowController*, const TableIter&);
    void setUpShadowControlCalcGravityFromCSV(ShadowController*, const TableIter&);
    void setUpShadowControlFromCSV(ShadowController*, LiveActor*, const TableIter&);
    ShadowController* createShadowControlFromCSV(LiveActor*, const TableIter&);
    void setUpShadowVolumeFromCSV(ShadowVolumeDrawer*, const TableIter&);
    ShadowSurfaceCircle* createShadowSurfaceCircleFromCSV(LiveActor*, const TableIter&);
    ShadowSurfaceOval* createShadowSurfaceOvalFromCSV(LiveActor*, const TableIter&);
    ShadowSurfaceBox* createShadowSurfaceBoxFromCSV(LiveActor*, const TableIter&);
    ShadowVolumeSphere* createShadowVolumeSphereFromCSV(LiveActor*, const TableIter&);
    ShadowVolumeOval* createShadowVolumeOvalFromCSV(LiveActor*, const TableIter&);
    ShadowVolumeOvalPole* createShadowVolumeOvalPoleFromCSV(LiveActor*, const TableIter&);
    ShadowVolumeCylinder* createShadowVolumeCylinderFromCSV(LiveActor*, const TableIter&);
    ShadowVolumeBox* createShadowVolumeBoxFromCSV(LiveActor*, const TableIter&);
    ShadowVolumeFlatModel* createShadowVolumeFlatModelFromCSV(LiveActor*, const TableIter&);
    ShadowVolumeLine* createShadowVolumeLineFromCSV(LiveActor*, const TableIter&);
    void addShadowFromCSV(LiveActor*, const TableIter&);
};  // namespace ActorShadow
