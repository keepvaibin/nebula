#pragma once


class FurCtrl;
class FurParam;
class J3DModel;
class J3DModelData;
class LiveActor;
struct ResTIMG;

class FurMulti {
public:
    FurMulti(LiveActor*, u32);

    void setLayerDirect(u32, u32, u32, u32, FurParam*, ResTIMG*, ResTIMG*, ResTIMG*, ResTIMG*);
    void addToManager();
    void offDraw(u32);
    void onDraw(u32);

    u8 mAddedToManager;
    u8 _1;
    u8 mCount;
    u8* mShapeIndices;
    u8* mDrawOrders;
    u32 _C;
    u32 _10;
    u32 _14;
    LiveActor* mActor;
    J3DModel* mModel;
    FurCtrl** mFurCtrls;
};

class FurBank {
public:
    FurBank() : mCount() {
        for (u32 i = 0; i < 32; i++) {
            mEntries[i] = nullptr;
            mLayerMasks[i] = 0;
        }
    }

    FurMulti* check(J3DModelData*, u32);
    void regist(FurMulti*, u32);

    u32 mCount;
    FurMulti* mEntries[32];
    u32 mLayerMasks[32];
};

namespace MR {
    FurMulti* initMultiFur(LiveActor*, s32);
};  // namespace MR
