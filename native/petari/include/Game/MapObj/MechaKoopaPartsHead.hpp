#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class MechaKoopaPartsHead : public MapObjActor {
public:
    MechaKoopaPartsHead(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&) override {};
    virtual void initCaseNoUseSwitchA(const MapObjActorInitInfo&) override {};
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&) override {};
    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo&) override {};

    void exeWait();
    void exeDemoBreak();
    void exeDemoWhiteFadeOut();
    void exeDemoWhiteWait();
    void exeDemoWhiteFadeIn();
    void exeDemoAppearStar();
};
