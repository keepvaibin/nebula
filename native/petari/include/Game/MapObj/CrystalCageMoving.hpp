#pragma once

#include "Game/MapObj/MapObjActor.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;
class ModelObj;

class CrystalCageMoving : public MapObjActor {
public:
    CrystalCageMoving(const char*);

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual void control() override;
    virtual void updateHitSensor(HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;
    virtual void connectToScene(const MapObjActorInitInfo&) override;

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
