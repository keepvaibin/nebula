#pragma once

#include "Game/MapObj/SimpleMapObj.hpp"

class GalaxyNamePlate;

class AstroMapObj : public MapObjActor {
public:
    AstroMapObj(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void control();

    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo&) {};
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&) {};
    virtual void initCaseNoUseSwitchA(const MapObjActorInitInfo&) {};
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&) {};

    void exeOpen();
    void exeWait();
    void exeRevival();

    void setStateAlive();
    void setStateDead();
    void selectNrvWait();
    bool isPlayMachineSE() const;
    void tryStartAllAnimAndEffect(const char* pAnimName);
    bool isEndRevival() const;
    void setStateDoorOpenOrClose();
    void startDemo();

    const Table* mNamePlateTable;
    GalaxyNamePlate* mNamePlate;
    // The placement name; MapObjActor::mObjectName is the model name, which can add the dome's.
    const char* mObjName;
    CollisionParts* mCloseCollision;
    CollisionParts* mOpenCollision;
    s32 mDomeId;
    const char* mEngineSound;
    const char* mRevivalSound;
    bool mIsDoor;
    bool mIsDomeEntrance;
    bool mIsStarPlate;
    bool mIsKitchenEntrance;
};

class AstroSimpleObj : public SimpleMapObjFarMax {
public:
    AstroSimpleObj(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void control();

    const Table* mNamePlateTable;
    GalaxyNamePlate* mNamePlate;
};
