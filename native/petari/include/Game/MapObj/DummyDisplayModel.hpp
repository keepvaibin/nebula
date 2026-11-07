#pragma once

#include "Game/LiveActor/PartsModel.hpp"

class LodCtrl;
class NameObjArchiveListCollector;

#define ITEM_TYPE_COIN 0
#define ITEM_TYPE_KINOPIO 1
#define ITEM_TYPE_STARPIECE 4
#define ITEM_TYPE_POWER_STAR 7
#define ITEM_TYPE_GRAND_STAR 13

struct DummyDisplayModelInfo {
    const char* mName;
    Vec _4;
    u32 _10;
    const char* mAnim;
    bool mHasColorChange;
};

class DummyDisplayModel : public PartsModel {
public:
    DummyDisplayModel(LiveActor*, const DummyDisplayModelInfo*, int, s32, s32);

    virtual void init(const TableIter&);
    virtual void makeActorAppeared();
    virtual void makeActorDead();
    virtual void control();
    virtual void calcAndSetBaseMtx();

    const DummyDisplayModelInfo* mModelInfo;
    s32 mItemType;
    s32 _A4;
    LodCtrl* _A8;
    bool _AC;
};

namespace MR {
    DummyDisplayModel* createDummyDisplayModel(LiveActor*, const TableIter&, s32, const Vec3f&, const Vec3f&);
    DummyDisplayModel* createDummyDisplayModel(LiveActor*, const TableIter&, const char*, s32);
    DummyDisplayModel* createDummyDisplayModel(LiveActor*, const TableIter&, MtxPtr, s32, const Vec3f&, const Vec3f&);
    DummyDisplayModel* createDummyDisplayModelCrystalItem(LiveActor*, const TableIter&, const Vec3f&, const Vec3f&);
    DummyDisplayModel* createDummyDisplayModelCrystalItem(LiveActor*, s32, const Vec3f&, const Vec3f&);
    s32 getDummyDisplayModelId(const TableIter&, s32);
    s32 getDummyDisplayModelId(const LiveActor*);
    void makeArchiveListDummyDisplayModel(NameObjArchiveListCollector*, const TableIter&);
};  // namespace MR
