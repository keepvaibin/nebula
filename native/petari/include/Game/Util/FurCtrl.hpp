#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/FurParam.hpp"
#include "Game/Render/Model.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/Array.hpp"

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
    void setupFur(Model* pModel, ResTIMG* pLength, ResTIMG* pBody, u32 shape, u32 numLayers);
    void setupFurClone(Model* pModel, FurCtrl* pOther);

    LiveActor* mActor;
    DynamicFurParam mDynamicParam;
    u8 mIsClone;
    Model* mModel;
    u32 mShapeIndex;
    FurParam* mParam;
    u8 mVisible;
    f32 mFurUVScale;
    u32 mNumLayers;
    FurDrawer* mDrawer;
    SiteMesh< VertexFur >* mMesh;
};

class FurDrawManager : public NameObj {
public:
    FurDrawManager(u32 capacity);

    virtual ~FurDrawManager() override;
    virtual void draw() const override;

    void add(FurCtrl* pCtrl, u8 index);

    FurBank* getFurBank() const {
        return mBank;
    }

    MR::Vector< MR::AssignableArray< FurCtrl* > > mFurCtrls[2];  // by draw order; only order 1 is drawn here
    FurBank* mBank;
};

namespace MR {
    FurDrawManager* getFurDrawManager();
};  // namespace MR
