#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class LiveActorGroup;

class KoopaBattleMapPlanet : public LiveActor {
public:
    KoopaBattleMapPlanet(const char* pName, const char* pModelName, bool isFinalPlanet, bool isNoPlate);

    virtual void init(const TableIter&) override;
    virtual MtxPtr getBaseMtx() const override {
        return const_cast< MtxPtr >(mBaseMtx.mMtx);
    }
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;

    void startAnimPowerStarAppear();
    void createPlate();

    void exeWait();
    void exeDemoGrandStarAppear();

private:
    const char* mModelName;
    Pos3f mBaseMtx;
    Vec3f mRotateMoment;
    LiveActorGroup* mDamagePlateGroup;
    LiveActorGroup* mCoinPlateGroup;
    LiveActorGroup* mPlateGroup;
    LiveActor* mDamagePlate;
    bool mIsFinalPlanet;
    bool mIsNoPlate;
};
