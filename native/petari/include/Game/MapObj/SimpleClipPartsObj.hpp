#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class SimpleClipPartsObj : public MapObjActor {
public:
    SimpleClipPartsObj(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;
    virtual void connectToScene(const MapObjActorInitInfo& rInfo) override;
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo& rInfo) override;
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo& rInfo) override;
    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo& rInfo) override;
};
