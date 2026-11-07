#include "Game/Map/OceanBowlBloomDrawer.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Map/OceanBowl.hpp"
#include "Game/Map/OceanBowlPoint.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"

OceanBowlBloomDrawer::OceanBowlBloomDrawer(OceanBowl* pHost) : NameObj("オーシャンボウル[ブルーム描画]"), mHost(pHost) {
}

void OceanBowlBloomDrawer::init(const TableIter&) {
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_OceanBowlBloomDrawer);
}

void OceanBowlBloomDrawer::draw() const {
    if (mHost->mIsClipped || !MR::isValidDraw(mHost) || !MR::isCameraInWater()) {
        return;
    }

    const SiteMaterial material = mHost->loadMaterialBloom();
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());

    OceanBowlPoint* pPoint2;
    u16 zero = 0;
    u16 one = 1;
    for (s32 x = 0; x < 24; x++) {
        // Texture coordinates 0 and 1 are entries of the bowl's arrays (two floats each), the vertex's index in both.
        VertexPCT2* vtx = site.vertices< VertexPCT2 >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 50);
        for (s32 y = 0; y < 25; y++) {
            pPoint2 = mHost->getPoint(x, y);
            OceanBowlPoint* pPoint = mHost->getPoint(x + 1, y);
            *vtx++ = {pPoint->mVertexPosition.x, pPoint->mVertexPosition.y, pPoint->mVertexPosition.z, Rgba8(0xFF, 0xFF, 0xFF, mHost->getPoint(x + 1, y)->mAlpha), mHost->mVtxTex0Array[2 * zero], mHost->mVtxTex0Array[2 * zero + 1], mHost->mVtxTex1Array[2 * zero], mHost->mVtxTex1Array[2 * zero + 1]};

            *vtx++ = {pPoint2->mVertexPosition.x, pPoint2->mVertexPosition.y, pPoint2->mVertexPosition.z, Rgba8(0xFF, 0xFF, 0xFF, mHost->getPoint(x, y)->mAlpha), mHost->mVtxTex0Array[2 * one], mHost->mVtxTex0Array[2 * one + 1], mHost->mVtxTex1Array[2 * one], mHost->mVtxTex1Array[2 * one + 1]};

            zero += 2;
            one += 2;
        }
    }
}
