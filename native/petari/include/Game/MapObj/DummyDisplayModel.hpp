#pragma once

#include "Game/LiveActor/PartsModel.hpp"

class LodCtrl;

// The table rows (arg 7 of the hosts) with special handling.
// Rows of the model table (placement arg 7).
// A model id the stage data names (MR::getDummyDisplayModelId reads it).
enum DummyDisplayModelId {
    DummyDisplayModelId_Coin = 0,
    DummyDisplayModelId_Kinopio = 1,
    DummyDisplayModelId_SpinDriver = 2,
    DummyDisplayModelId_SuperSpinDriver = 3,
    DummyDisplayModelId_StarPiece = 4,
    DummyDisplayModelId_Tico = 5,
    DummyDisplayModelId_KeySwitch = 6,
    DummyDisplayModelId_PowerStar = 7,
    DummyDisplayModelId_KinokoOneUp = 8,
    DummyDisplayModelId_Kuribo = 9,
    DummyDisplayModelId_BlueChip = 10,
    DummyDisplayModelId_YellowChip = 11,
    DummyDisplayModelId_StrayTico = 12,
    DummyDisplayModelId_GrandStar = 13,
    DummyDisplayModelId_KinokoLifeUp = 14
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

    virtual void init(const TableIter&) override;
    virtual void makeActorAppeared() override;
    virtual void makeActorDead() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;

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
