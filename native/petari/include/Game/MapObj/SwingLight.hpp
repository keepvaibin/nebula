#pragma once

#include "Game/MapObj/MapObjActor.hpp"
#include "Game/Util/Color.hpp"

class VolumeModelDrawer;

class LightCylinder : public MapObjActor {
public:
    LightCylinder(const char*, MtxPtr, const Color8&);

    virtual void init(const JMapInfoIter&);
    virtual void draw() const;
    virtual void calcAndSetBaseMtx();
    virtual void connectToScene(const MapObjActorInitInfo&);
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&) {
    }

    virtual void initCaseNoUseSwitchA(const MapObjActorInitInfo&) {
    }

    void initLightCylinderByOwner(const JMapInfoIter&);
    void initLightVolume(const char*);

    MtxPtr mHostMtx;
    VolumeModelDrawer* mLightVolume;
    Color8 mColor;
};

class SwingLight : public MapObjActor {
public:
    SwingLight(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&);
    virtual void initCaseNoUseSwitchA(const MapObjActorInitInfo&);
    virtual void makeSubModels(const JMapInfoIter&, const MapObjActorInitInfo&);

    void appearLight();

    LightCylinder* mLightCylinder;
};
