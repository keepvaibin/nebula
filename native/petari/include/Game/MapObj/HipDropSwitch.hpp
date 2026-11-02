#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class CollisionParts;
class MapObjConnector;
class SpringValue;

class HipDropSwitch : public LiveActor {
public:
    HipDropSwitch(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void calcAnim();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void initStarPointerTarget();
    bool trySwitchDown();
    bool tryOn();

    void exeOff();
    void exeSwitchDown();
    void exeOn();

    u32 _8C;  // useless
    SpringValue* mSpringValue;
    MapObjConnector* mConnector;
    CollisionParts* mSpringModel;
    TPos3f _9C;
    bool _CC;
    bool _CD;
    bool _CE;
};
