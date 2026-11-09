#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class LodCtrl;
class MapObjActorInitInfo;
class MapPartsRailMover;
class MapPartsRotatorBase;
class MapPartsRailRotator;
class MapPartsRailGuideDrawer;
class MapPartsRailPosture;
class ModelObj;
class MapPartsFunction;
class ProjmapEffectMtxSetter;

class MapObjActor : public LiveActor {
public:
    MapObjActor(const char* pName);
    MapObjActor(const char* pName, const char* pObjName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void connectToScene(const MapObjActorInitInfo& rInfo);
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo& rInfo);
    virtual void initCaseNoUseSwitchA(const MapObjActorInitInfo& rInfo);
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo& rInfo);
    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo& rInfo);
    virtual void makeSubModels(const TableIter& /*rIter*/, const MapObjActorInitInfo& /*rInfo*/) {
    }

    void initialize(const TableIter& rIter, const MapObjActorInitInfo& rInfo);
    bool isObjectName(const char* pName) const;
    bool tryCreateBreakModel(const MapObjActorInitInfo& rInfo);
    bool tryEmitWaitEffect();
    bool tryDeleteWaitEffect();
    void startMapPartsFunctions();
    void endMapPartsFunctions();
    void pauseMapPartsFunctions();
    void setStateWait();
    void updateProjmapMtx();
    void exeWait();
    void exeMove();
    void exeDone();

    const char* mObjectName;
    LodCtrl* mPlanetLodCtrl;
    ModelObj* mBloomModel;
    ModelObj* mModelObj;
    ProjmapEffectMtxSetter* mMatrixSetter;
    MapPartsRailMover* mRailMover;
    MapPartsRotatorBase* mRotator;
    MapPartsRailRotator* mRailRotator;
    MapPartsRailPosture* mRailPosture;
    MapPartsRailGuideDrawer* mRailGuideDrawer;
    u8 mIsProjmapEveryFrame;
    u8 mIsMirrorFromModel;
    u8 mPlayRiddleSE;
    Nerve* mWaitNrv;
    Nerve* mMoveNrv;
    Nerve* mDoneNrv;

private:
    // The stage-effect moving sound for mStageEffectName; looked up again when mObjectName changes.
    const char* mStageEffectName;
    const char* mStageEffectMovingSe;
};

class MapObjActorUtil {
public:
    static void startAllMapPartsFunctions(const MapObjActor* pActor);
    static void endAllMapPartsFunctions(const MapObjActor* pActor);
    static void pauseAllMapPartsFunctions(const MapObjActor* pActor);
    static void resumeAllMapPartsFunctions(const MapObjActor* pActor);
    static bool isRotatorMoving(const MapObjActor* pActor);
    static bool isRailMoverWorking(const MapObjActor* pActor);
    static bool isRailMoverReachedEnd(const MapObjActor* pActor);
    static f32 getSeesaw1AxisAngularSpeed(const MapObjActor* pActor);
    static void forceRotateSeesaw1Axis(const MapObjActor* pActor, f32);

    static void startRotator(const MapObjActor* pActor);
    static void startRailMover(const MapObjActor* pActor);
    static void endRotator(const MapObjActor* pActor);
    static void pauseRotator(const MapObjActor* pActor);
    static void resetRailMoverToInitPos(const MapObjActor* pActor);
    static void startBreak(MapObjActor* pActor);
    static bool tryStartBreak(MapObjActor* pActor);
    static bool isBreakStopped(const MapObjActor* pActor);
    static void killBloomModel(MapObjActor* pActor);
    static void appearBloomModel(MapObjActor* pActor);

    static void setupInitInfoTypical(MapObjActorInitInfo* pInfo, const char* pName);
    static void setupInitInfoColorChangeArg0(MapObjActorInitInfo* pInfo, const TableIter& rIter);
    static void setupInitInfoTextureChangeArg1(MapObjActorInitInfo* pInfo, const TableIter& rIter);
    static void setupInitInfoShadowLengthArg2(MapObjActorInitInfo* pInfo, const TableIter& rIter);
    static void setupInitInfoSeesaw(MapObjActorInitInfo* pInfo, const TableIter& rIter, const char* pName, f32);
    static void setupInitInfoSimpleMapObj(MapObjActorInitInfo* pInfo);
    static void setupInitInfoPlanet(MapObjActorInitInfo* pInfo);
};
