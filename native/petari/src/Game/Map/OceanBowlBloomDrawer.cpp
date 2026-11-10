#include "Game/Map/OceanBowlBloomDrawer.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Map/OceanBowl.hpp"
#include "Game/Map/OceanBowlPoint.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

OceanBowlBloomDrawer::OceanBowlBloomDrawer(OceanBowl* pHost) : NameObj("オーシャンボウル[ブルーム描画]"), mHost(pHost) {
    lumacore::setClass(this, lumacore::kClassId< OceanBowlBloomDrawer >);
}

void OceanBowlBloomDrawer::init(const TableIter& /*rIter*/) {
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_OceanBowlBloomDrawer);
}

void OceanBowlBloomDrawer::draw() const {
    if (mHost->mIsClipped || !MR::isValidDraw(mHost) || !MR::isCameraInWater()) {
        return;
    }

    const SiteMaterial material = mHost->loadMaterialBloom();
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());

    const f32* pTex0 = mHost->mVtxTex0Array;
    const f32* pTex1 = mHost->mVtxTex1Array;
    for (s32 x = 0; x < OceanBowl::cPointNum - 1; x++) {
        VertexPCT2* vtx = site.vertices< VertexPCT2 >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, OceanBowl::cPointNum * 2);
        for (s32 y = 0; y < OceanBowl::cPointNum; y++) {
            const OceanBowlPoint* pNext = mHost->getPoint(x + 1, y);
            const OceanBowlPoint* pCur = mHost->getPoint(x, y);
            const s32 tex = OceanBowl::getStripTexCoordIndex(x, y);

            *vtx++ = {pNext->mVertexPosition.x, pNext->mVertexPosition.y, pNext->mVertexPosition.z, Rgba8(0xFF, 0xFF, 0xFF, pNext->mAlpha),
                      pTex0[tex], pTex0[tex + 1], pTex1[tex], pTex1[tex + 1]};

            *vtx++ = {pCur->mVertexPosition.x, pCur->mVertexPosition.y, pCur->mVertexPosition.z, Rgba8(0xFF, 0xFF, 0xFF, pCur->mAlpha),
                      pTex0[tex + 2], pTex0[tex + 3], pTex1[tex + 2], pTex1[tex + 3]};
        }
    }
}
