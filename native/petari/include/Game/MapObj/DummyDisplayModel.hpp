#pragma once

#include "Game/LiveActor/PartsModel.hpp"

class LodCtrl;

// The table rows (arg 7 of the hosts) with special handling.
enum DummyDisplayModelId {
    DummyDisplayModelId_Coin = 0,
    DummyDisplayModelId_Kinopio = 1,
    DummyDisplayModelId_StarPiece = 4,
    DummyDisplayModelId_PowerStar = 7,
    DummyDisplayModelId_GrandStar = 13
};

struct DummyDisplayModelInfo {
    const char* mName;
    Vec mOffset;
    u32 mDrawBufferType;
    const char* mAnim;
    bool mHasColorChange;
};

class DummyDisplayModel : public PartsModel {
public:
    DummyDisplayModel(LiveActor*, const DummyDisplayModelInfo*, int drawBufferType, s32 modelId, s32 colorFrame);

    virtual void init(const TableIter&);
    virtual void makeActorAppeared();
    virtual void makeActorDead();
    virtual void control();
    virtual void calcAndSetBaseMtx();

    const DummyDisplayModelInfo* mModelInfo;
    s32 mItemType;  // DummyDisplayModelId
    s32 mBrkFrame;
    LodCtrl* mLodCtrl;
    bool mIsInCrystal;
};

namespace MR {
    DummyDisplayModel* createDummyDisplayModel(LiveActor*, const TableIter&, s32, const Vec3f&, const Vec3f&);
    DummyDisplayModel* createDummyDisplayModel(LiveActor*, const TableIter&, const char*, s32);
    DummyDisplayModel* createDummyDisplayModel(LiveActor*, const TableIter&, MtxPtr, s32, const Vec3f&, const Vec3f&);
    DummyDisplayModel* createDummyDisplayModelCrystalItem(LiveActor*, const TableIter&, const Vec3f&, const Vec3f&);
    DummyDisplayModel* createDummyDisplayModelCrystalItem(LiveActor*, s32, const Vec3f&, const Vec3f&);
    s32 getDummyDisplayModelId(const TableIter&, s32);
    s32 getDummyDisplayModelId(const LiveActor*);
};  // namespace MR
