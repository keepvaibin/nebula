#include "Game/MapObj/ClipFieldFillDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include "Game/Render/TypedDraw.hpp"
#include <nebula_game_api.h>

ClipFieldFillDraw::ClipFieldFillDraw(const char* pName)
    : NameObj(pName), mFillBlendOp(NEBULA_BLEND_ADD), mFillSrcFactor(NEBULA_FACTOR_SRC_ALPHA), mFillDstFactor(NEBULA_FACTOR_ONE),
      _1C(255, 230, 80, 200), _20(255, 255, 255, 255), _24(), _25() {
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_ClipFieldFillDraw);
}

void ClipFieldFillDraw::init(const TableIter& rIter) {
    if (MR::isValidInfo(rIter)) {
        return;
    }
}

void ClipFieldFillDraw::setModeSubColor() {
    mFillBlendOp = NEBULA_BLEND_SUBTRACT;
    mFillDstFactor = NEBULA_FACTOR_ONE;
    mFillSrcFactor = NEBULA_FACTOR_ONE;
}

void ClipFieldFillDraw::setUpFillScreen() const {
    Mtx44 projectionMtx;
    NebulaMtxOrtho(projectionMtx, 0.0f, MR::getScreenHeight(), 0.0f, MR::getScreenWidth(), -1.0f, 1.0f);
    Frame::setOrthographic(projectionMtx);
}

void ClipFieldFillDraw::sendFillScreen(NebulaColor8 color) const {
    // The colour where the volumes left the stencil set (_25: or where they did not), depth neither tested nor written.
    SiteMaterial material;
    material.init(NEBULA_SHADER_FLAT_COLOR, SiteMaterial::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false,
                       NEBULA_CULL_BACK);
    material.mState.blend_op = mFillBlendOp;
    material.mState.blend_src = mFillSrcFactor;
    material.mState.blend_dst = mFillDstFactor;
    material.setColor(0, color.r, color.g, color.b, color.a);
    material.mState.alpha_write = 0;
    material.setStencil(_25 ? NEBULA_STENCIL_IF_SET : NEBULA_STENCIL_IF_ZERO, NEBULA_STENCIL_KEEP);

    TypedDraw site = TypedDraw::eyeSpace(material, cIdentityMtx);
    VertexPT* vtx = site.vertices< VertexPT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 4);
    {
        u16 screenWidth = MR::getScreenWidth();
        u16 screenHeight = MR::getScreenHeight();

        *vtx++ = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

        *vtx++ = {static_cast< f32 >(static_cast< u16 >(screenWidth)), 0.0f, 0.0f, 1.0f, 0.0f};

        *vtx++ = {0.0f, static_cast< f32 >(static_cast< u16 >(screenHeight)), 0.0f, 0.0f, 1.0f};

        *vtx++ = {static_cast< f32 >(static_cast< u16 >(screenWidth)), static_cast< f32 >(static_cast< u16 >(screenHeight)), 0.0f, 1.0f, 1.0f};
    }
}

void ClipFieldFillDraw::draw() const {
    setUpFillScreen();
    sendFillScreen(_1C);
    MR::loadViewMtx();
    MR::loadProjectionMtx();
    Frame::setZScaleOffset(1.0f, 0.0f);
    MR::clearStencil();
    MR::loadViewMtx();
    MR::loadProjectionMtx();
}

namespace MR {
    NameObj* createClipFieldFillDraw(const char* pParam1) {
        return static_cast< ClipFieldFillDraw* >(MR::createSceneObj(SceneObj_ClipFieldFillDraw));
    }

    NameObj* createVolumeDrawSpotLight(const char* pParam1) {
        ClipFieldFillDraw* clipFieldFillDraw;

        clipFieldFillDraw = static_cast< ClipFieldFillDraw* >(createClipFieldFillDraw(pParam1));
        clipFieldFillDraw->_25 = false;
        clipFieldFillDraw->_24 = false;
        clipFieldFillDraw->setModeSubColor();
        clipFieldFillDraw->_1C = static_cast< u32 >(Color8(70, 35, 11, 80));
        clipFieldFillDraw->_20 = static_cast< u32 >(Color8(255, 255, 128, 80));

        return clipFieldFillDraw;
    }
};  // namespace MR
