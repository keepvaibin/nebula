#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/J3DGraphBase/J3DTexture.hpp>
#include <JSystem/JGeometry/TMatrix.hpp>

enum MiniatureGalaxyType {
    MiniatureGalaxyType_Normal,
    MiniatureGalaxyType_Hatena,
    MiniatureGalaxyType_Koopa,
};

enum MiniatureGalaxyState {
    MiniatureGalaxyState_Hatena,
    MiniatureGalaxyState_Unknown,
    MiniatureGalaxyState_New,
    MiniatureGalaxyState_Open,
};

class AstroDomeOrbit;
class GalaxyNamePlate;
class ModelObj;
class NameObjArchiveListCollector;
class ProjmapEffectMtxSetter;

class MiniatureGalaxy : public LiveActor {
public:
    MiniatureGalaxy(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void appear();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    static void makeArchiveList(NameObjArchiveListCollector*, const JMapInfoIter&);

    void initPartsModel();
    void setScale(f32);
    void tryZoomIn(bool);
    void tryZoomOut();
    bool isZoomEnd() const;
    void updateZoomInOut();
    void updateNamePlate();
    void appearUnknownModel(const char*);
    void showUnknownModel();
    void showSelectModel();
    void hideUnknownModel();
    void appearSelectModel();
    void disappearSelectModel();
    void showNamePlate(bool);
    bool isUseKoopaFaceModel() const;
    void playPointedME();
    void playNeedStarME();

    void exeWait();
    void exeSelected();
    void exeNotSelected();
    void exeConfirmed();
    void exeOpen();

    s32 mType;
    s32 mState;
    ModelObj* mUnknownModel;
    ModelObj* mShadowModel;
    ModelObj* mSelectModel;
    ModelObj* mStarPlateModel;
    TPos3f mPosMtx;
    TPos3f mShadowBaseMtx;
    ProjmapEffectMtxSetter* mProjmapEffectMtxSetter;
    TVec3f mInitPos;
    const char* mObjectName;
    const char* mGalaxyName;
    s32 mCanZoomIn;
    s32 mZoomLevel;
    J3DTexMtx mOnesTexMtx;
    J3DTexMtx mTensTexMtx;
    AstroDomeOrbit* mOrbit;
    GalaxyNamePlate* mNamePlate;
};
