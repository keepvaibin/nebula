#pragma once

#include "Game/LiveActor/PartsModel.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class StarPiece;

// This class may not belong in this file
class RotPartsModel : public PartsModel {
public:
    RotPartsModel(LiveActor* a1, const char* a2, const char* a3, MtxPtr a4, int a5, bool a6) : PartsModel(a1, a2, a3, a4, a5, a6) {
    }

    virtual ~RotPartsModel() {
    }

    virtual void calcAndSetBaseMtx() {
        PartsModel::calcAndSetBaseMtx();
        TRot3f mtx;
        mtx.identity();

        mtx.setRotateDegree(mRotation);

        NebulaMtxConcat(getBaseMtx(), mtx, getBaseMtx());
    }
};

class ItemBubble : public LiveActor {
public:
    ItemBubble(const char*);

    virtual ~ItemBubble();
    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();
    virtual void appear();
    virtual void kill();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeWait();
    void exeBreak();

    const TVec3f& getRotPartsPosition(int index) const {
        return _90[index]->mPosition;
    }

    StarPiece* getStarPiece(int index) const {
        return _94[index];
    }

    f32 _8C;
    RotPartsModel** _90;
    StarPiece** _94;
    u16 mItemCount;
    u16 mItemType;
    TPos3f _9C;
    TPos3f _CC;
    TVec3f _FC;
    TVec3f _108;
    f32 mRailSpeed;
    bool mUseRail;
};
