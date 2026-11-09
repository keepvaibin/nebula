#pragma once

#include "Game/MapObj/MapObjActor.hpp"
#include "Game/Util/Color.hpp"

class VolumeModelDrawer;

class LightCylinder : public MapObjActor {
public:
    LightCylinder(const char*, MtxPtr, const Color8&);

    virtual void init(const TableIter&) override;
    virtual void draw() const override;
    virtual void calcAndSetBaseMtx() override;
    virtual void connectToScene(const MapObjActorInitInfo&) override;
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&) override {
    }

    virtual void initCaseNoUseSwitchA(const MapObjActorInitInfo&) override {
    }

    void initLightCylinderByOwner(const TableIter&);
    void initLightVolume(const char*);

    MtxPtr mHostMtx;
    VolumeModelDrawer* mLightVolume;
    Color8 mColor;
};

class SwingLight : public MapObjActor {
public:
    SwingLight(const char*);

    virtual void init(const TableIter&) override;
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&) override;
    virtual void initCaseNoUseSwitchA(const MapObjActorInitInfo&) override;
    virtual void makeSubModels(const TableIter&, const MapObjActorInitInfo&) override;

    void appearLight();

    LightCylinder* mLightCylinder;
};
