#pragma once
#include "Game/Util/Array.hpp"


class FurCtrl;
class FurParam;
class Model;
class ModelData;
class LiveActor;
struct ResTIMG;

class FurMulti {
public:
    FurMulti(LiveActor*, u32);

    void setLayerDirect(u32 layer, u32 shape, u32 numLayers, FurParam* pParam, ResTIMG* pBody, ResTIMG* pLength, ResTIMG* pDensity);
    void addToManager();
    void offDraw(u32);
    void onDraw(u32);

    u32 mCount;
    LiveActor* mActor;
    Model* mModel;
    FurCtrl** mFurCtrls;
};

class FurBank {
public:
    FurBank() : mCount() {
    }

    FurMulti* check(ModelData*, u32);
    void regist(FurMulti*, u32);

    u32 mCount;
    MR::GrowTable< FurMulti*, 32 > mEntries;
    MR::GrowTable< u32, 32 > mLayerMasks;
};

namespace MR {
    FurMulti* initMultiFur(LiveActor*, s32);
};  // namespace MR
