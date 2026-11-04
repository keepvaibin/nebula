#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/FurParam.hpp"
#include "Game/Util/RenderSignals.hpp"
#include <JSystem/J3DGraphAnimator/J3DModel.hpp>
#include "Game/System/NebulaArena.hpp"
#include <cstring>

class CShader;
class FurBank;
class FurDrawer;
class LiveActor;
struct ResTIMG;

class J3DModel2 : public J3DModel {
public:
    J3DModel2(J3DModel* pModel);

    virtual ~J3DModel2();

    void setVtxShader(J3DVtxShader* pShader, s32 componentSize) {
        mUnkCalc1 = pShader;

        if (pShader != nullptr) {
            mUnkCalc1->setup(getModelData());

            if (mVertexBuffer.mTransformedVtxPosArray[0] == nullptr || mVertexBuffer.mTransformedVtxPosArray[1] == nullptr) {
                u32 size = (componentSize * (getVertexBuffer()->getVertexData()->getVtxNum() * 3) + 31) & ~31;
                mVertexBuffer.mTransformedVtxPosArray[0] = new u8[size];

                if (mVertexBuffer.mTransformedVtxPosArray[0] != nullptr) {
                    mVertexBuffer.mTransformedVtxPosArray[1] = mVertexBuffer.mTransformedVtxPosArray[0];
                }
            }

            for (s32 j = 0; j < 2; j++) {
                s32 componentBytes = componentSize * getVertexBuffer()->getVertexData()->getVtxNum();
                memcpy(mVertexBuffer.mTransformedVtxPosArray[j], getVertexBuffer()->getVertexData()->getVtxPosArray(), componentBytes * 3);
                NebulaRender::memoryRewritten(mVertexBuffer.mTransformedVtxPosArray[j], componentSize * (getVertexBuffer()->getVertexData()->getVtxNum() * 3));
            }
        }
    }
};

class FurCtrl {
public:
    FurCtrl(LiveActor* pActor, FurParam* pParam, bool addToManager, u8 drawOrder);

    void calcLayerForm();
    void drawFur();
    void createFurMap();
    void setupFur(J3DModel* pModel, ResTIMG* pLength, ResTIMG* pIndirect, ResTIMG* pBody, u16 shape, u8 numLayers);
    void setupFurClone(J3DModel* pModel, FurCtrl* pOther);

    LiveActor* mActor;
    DynamicFurParam mDynamicParam;
    u8 mIsClone;
    u8 _D;
    u8 _E;
    u8 _F;
    J3DModel* mModel;
    u16 mShapeIndex;
    FurParam* mParam;
    u8 mVisible;
    f32 mFurUVScale;
    u16 mNumLayers;
    FurDrawer* mDrawer;
    CShader* mShader;
    J3DModel** mLayerModels;
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
