#include "Game/Map/OceanBowlBloomDrawer.hpp"
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/Map/OceanBowl.hpp"
#include "Game/Map/OceanBowlPoint.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include <revolution/gx/GXVert.h>

OceanBowlBloomDrawer::OceanBowlBloomDrawer(OceanBowl* pHost) : NameObj("\203\111\201\133\203\126\203\203\203\223\203\173\203\105\203\213[\203\165\203\213\201\133\203\200\225\140\211\346]" /* オーシャンボウル[ブルーム描画] */), mHost(pHost) {
}

void OceanBowlBloomDrawer::init(const JMapInfoIter&) {
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_OceanBowlBloomDrawer);
}

void OceanBowlBloomDrawer::draw() const {
    if (mHost->mIsClipped || !MR::isValidDraw(mHost) || !MR::isCameraInWater()) {
        return;
    }

    mHost->loadMaterialBloom();

    OceanBowlPoint* pPoint2;
    u16 zero = 0;
    u16 one = 1;
    for (s32 x = 0; x < 24; x++) {
        // Texture coordinates 0 and 1 are entries of the bowl's arrays (two floats each), the vertex's index in both.
        J3DNativeDraw::begin(GX_TRIANGLESTRIP, 50, NEBULA_SHAPE_COLOR0 | NEBULA_SHAPE_TEXCOORD0 | NEBULA_SHAPE_TEXCOORD0 << 1);
        for (s32 y = 0; y < 25; y++) {
            pPoint2 = mHost->getPoint(x, y);
            OceanBowlPoint* pPoint = mHost->getPoint(x + 1, y);
            J3DNativeDraw::position(pPoint->mVertexPosition.x, pPoint->mVertexPosition.y, pPoint->mVertexPosition.z);
            J3DNativeDraw::color(0xFF, 0xFF, 0xFF, mHost->getPoint(x + 1, y)->mAlpha);
            J3DNativeDraw::texCoord(mHost->mVtxTex0Array[2 * zero], mHost->mVtxTex0Array[2 * zero + 1]);
            J3DNativeDraw::texCoord(mHost->mVtxTex1Array[2 * zero], mHost->mVtxTex1Array[2 * zero + 1]);

            J3DNativeDraw::position(pPoint2->mVertexPosition.x, pPoint2->mVertexPosition.y, pPoint2->mVertexPosition.z);
            J3DNativeDraw::color(0xFF, 0xFF, 0xFF, mHost->getPoint(x, y)->mAlpha);
            J3DNativeDraw::texCoord(mHost->mVtxTex0Array[2 * one], mHost->mVtxTex0Array[2 * one + 1]);
            J3DNativeDraw::texCoord(mHost->mVtxTex1Array[2 * one], mHost->mVtxTex1Array[2 * one + 1]);

            zero += 2;
            one += 2;
        }
    }
}
