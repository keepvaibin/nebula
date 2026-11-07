#pragma once

#include "Game/Render/Material.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

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

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void appear();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    static void makeArchiveList(NameObjArchiveListCollector*, const TableIter&);

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
    Pos3f mPosMtx;
    Pos3f mShadowBaseMtx;
    ProjmapEffectMtxSetter* mProjmapEffectMtxSetter;
    Vec3f mInitPos;
    const char* mObjectName;
    const char* mGalaxyName;
    s32 mCanZoomIn;
    s32 mZoomLevel;
    CoordMatrix mOnesTexMtx;
    CoordMatrix mTensTexMtx;
    AstroDomeOrbit* mOrbit;
    GalaxyNamePlate* mNamePlate;
};
