#pragma once

#include "Game/Render/Material.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

enum MiniatureGalaxyType {
    MiniatureGalaxyType_Normal,
    MiniatureGalaxyType_Hatena,
    MiniatureGalaxyType_Koopa,
};

// Also the state index GalaxySelectInfo shows.
enum MiniatureGalaxyState {
    MiniatureGalaxyState_Hatena,
    MiniatureGalaxyState_Unknown,
    MiniatureGalaxyState_New,
    MiniatureGalaxyState_Open,
};

class AstroDomeOrbit;
class GalaxyNamePlate;
class ModelObj;
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

    void initPartsModel();
    void setScale(f32);
    void tryZoomIn(bool isDisappearSelect);
    void tryZoomOut();
    bool isZoomEnd() const;
    void updateZoomInOut();
    void updateNamePlate();
    void appearUnknownModel(const char*);
    void showUnknownModel();
    void showSelectModel();
    void hideUnknownModel();
    void showOpenModel();
    void hideOpenModel();
    void appearSelectModel();
    void disappearSelectModel();
    // isAppearNow false leaves the plate ready but hidden.
    void showNamePlate(bool isAppearNow);
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
    ModelObj* mMysteryModel;
    ModelObj* mShadowModel;
    ModelObj* mSelectModel;
    ModelObj* mStarPlateModel;
    Pos3f mPosMtx;
    Pos3f mShadowBaseMtx;
    ProjmapEffectMtxSetter* mProjmapEffectMtxSetter;
    const char* mObjectName;
    const char* mGalaxyName;
    // Zoom target; mZoomStep counts up to the zoom frame count.
    bool mIsZoomOut;
    s32 mZoomStep;
    CoordMatrix mOnesTexMtx;
    CoordMatrix mTensTexMtx;
    AstroDomeOrbit* mOrbit;
    GalaxyNamePlate* mNamePlate;
};
