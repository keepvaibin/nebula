#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/FurParam.hpp"
#include "Game/Render/Model.hpp"
#include "Game/System/NebulaArena.hpp"

template < class T >
class SiteMesh;
struct VertexFur;
class FurBank;
class FurDrawer;
class LiveActor;
struct ResTIMG;

class FurCtrl {
public:
    FurCtrl(LiveActor* pActor, FurParam* pParam, bool addToManager, u8 drawOrder);

    void calcLayerForm();
    void drawFur();
    void createFurMap();
    void setupFur(Model* pModel, ResTIMG* pLength, ResTIMG* pIndirect, ResTIMG* pBody, u32 shape, u32 numLayers);
    void setupFurClone(Model* pModel, FurCtrl* pOther);

    LiveActor* mActor;
    DynamicFurParam mDynamicParam;
    u8 mIsClone;
    u8 _D;
    u8 _E;
    u8 _F;
    Model* mModel;
    u32 mShapeIndex;
    FurParam* mParam;
    u8 mVisible;
    f32 mFurUVScale;
    u32 mNumLayers;
    FurDrawer* mDrawer;
    SiteMesh< VertexFur >* mMesh;
    ResTIMG* mLengthMap;
    ResTIMG* mBodyMap;
    ResTIMG* mIndirectMap;
};

class FurDrawManager : public NameObj {
public:
    FurDrawManager(u8 capacity);

    virtual ~FurDrawManager();
    virtual void draw() const;

    void add(FurCtrl* pCtrl, u8 index);

    FurBank* getFurBank() const {
        return mBank;
    }

    u8 mCapacity;
    u8 mCounts[2];
    FurCtrl** mFurCtrls[2];
    FurBank* mBank;
};

namespace MR {
    FurDrawManager* getFurDrawManager();
};  // namespace MR
