#pragma once

#include "Game/MapObj/MapObjActor.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;
class ModelObj;

class CrystalCageMoving : public MapObjActor {
public:
    CrystalCageMoving(const char*);

    virtual void init(const TableIter&);
    virtual void kill();
    virtual void control();
    virtual void updateHitSensor(HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);
    virtual void connectToScene(const MapObjActorInitInfo&);

    void exeWaitBig();
    void exeBreakBig();
    void exeWaitSmall();
    void exeBreakSmall();
    void exeBreakAll();
    void exeDemoTicoMove();
    void exeDemoTicoStop();
    void exeDemoTicoChange();
    void endBreakBig();
    void crashMario(HitSensor*, HitSensor*);
    void initDummyModel(const TableIter&);
    void startBreakDemo();
    bool isNerveTypeEnd() const;

    ModelObj* mTicoModel;
    Pos3f mDummyModelMtx;
    ActorCameraInfo* mCameraInfo;
    Vec3f mStartPos;
    u8 mIsCoreShown;
};
