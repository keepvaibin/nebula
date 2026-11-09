#pragma once

#include "Game/LiveActor/PartsModel.hpp"
#include <nebula_geometry.hpp>

class StarPiece;

class RotPartsModel : public PartsModel {
public:
    RotPartsModel(LiveActor* pActor, const char* pName, const char* pModelName, MtxPtr pMtx, int drawBufferType, bool deferMaterials)
        : PartsModel(pActor, pName, pModelName, pMtx, drawBufferType, deferMaterials) {
    }

    virtual ~RotPartsModel() override {
    }

    virtual void calcAndSetBaseMtx() override {
        PartsModel::calcAndSetBaseMtx();
        Rot3f mtx;
        mtx.identity();

        mtx.setRotateDegree(mRotation);

        NebulaMtxConcat(getBaseMtx(), mtx, getBaseMtx());
    }
};

class ItemBubble : public LiveActor {
public:
    ItemBubble(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void exeWait();
    void exeBreak();

    const Vec3f& getRotPartsPosition(int index) const {
        return mCoins[index]->mPosition;
    }

    StarPiece* getStarPiece(int index) const {
        return mStarPieces[index];
    }

    f32 mSwing;
    RotPartsModel** mCoins;
    StarPiece** mStarPieces;
    u32 mItemCount;
    u32 mItemType;
    Pos3f mTargetMtx;
    Pos3f mItemMtx;
    Vec3f mClipCenter;
    Vec3f mBasePos;
    f32 mRailSpeed;
    bool mUseRail;
};
