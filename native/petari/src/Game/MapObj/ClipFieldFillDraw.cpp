#include "Game/MapObj/ClipFieldFillDraw.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/JMapUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include "Game/Util/NativeShader.hpp"
#include <nebula_game_api.h>

ClipFieldFillDraw::ClipFieldFillDraw(const char* pName)
    : NameObj(pName), mFillBlendOp(NEBULA_BLEND_ADD), mFillSrcFactor(NEBULA_FACTOR_SRC_ALPHA), mFillDstFactor(NEBULA_FACTOR_ONE),
      _1C(255, 230, 80, 200), _20(255, 255, 255, 255), _24(), _25() {
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_ClipFieldFillDraw);
}

void ClipFieldFillDraw::init(const JMapInfoIter& rIter) {
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
    J3DNativeView::setOrthographic(projectionMtx);

    TMtx34f posMtx;
    posMtx.identity();
    J3DNativeShape::setPosition(0, (MtxPtr)&posMtx);
    J3DPass::positionMatrix(0);

    J3DPass::writeColor(true);
    J3DPass::writeAlpha(false);
}

void ClipFieldFillDraw::sendFillScreen(NebulaColor8 color) const {
    // The colour where the volumes left the stencil set (_25: or where they did not), depth neither tested nor written.
    NebulaNativeMaterial material;
    NativeShader::init(material, NEBULA_SHADER_FLAT_COLOR, NativeShader::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false,
                       NEBULA_CULL_BACK);
    material.blend_op = mFillBlendOp;
    material.blend_src = mFillSrcFactor;
    material.blend_dst = mFillDstFactor;
    NativeShader::setColor(material, 0, color.r, color.g, color.b, color.a);
    NativeShader::send(material);
    J3DPass::stencil(_25 ? NEBULA_STENCIL_IF_SET : NEBULA_STENCIL_IF_ZERO, NEBULA_STENCIL_KEEP);
    J3DNativeShape::setCurrentIdentity();

    J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 4, NEBULA_SHAPE_TEXCOORD0);
    {
        u16 screenWidth = MR::getScreenWidth();
        u16 screenHeight = MR::getScreenHeight();

        J3DNativeDraw::position(0.0f, 0.0f, 0.0f);
        J3DNativeDraw::texCoord(0.0f, 0.0f);

        J3DNativeDraw::position(static_cast< f32 >(static_cast< u16 >(screenWidth)), 0.0f, 0.0f);
        J3DNativeDraw::texCoord(1.0f, 0.0f);

        J3DNativeDraw::position(0.0f, static_cast< f32 >(static_cast< u16 >(screenHeight)), 0.0f);
        J3DNativeDraw::texCoord(0.0f, 1.0f);

        J3DNativeDraw::position(static_cast< f32 >(static_cast< u16 >(screenWidth)), static_cast< f32 >(static_cast< u16 >(screenHeight)), 0.0f);
        J3DNativeDraw::texCoord(1.0f, 1.0f);
    }
}

void ClipFieldFillDraw::draw() const {
    setUpFillScreen();
    sendFillScreen(_1C);
    MR::loadViewMtx();
    MR::loadProjectionMtx();
    J3DNativeView::setZScaleOffset(1.0f, 0.0f);
    MR::clearStencil();
    MR::loadViewMtx();
    MR::loadProjectionMtx();
    J3DPass::writeAlpha(false);
    J3DPass::writeColor(true);

    if (_24) {
        NebulaNativeMaterial material;
        TDDraw::setupNative(&material, 0, 1, 0);
        material.cull = NEBULA_CULL_FRONT;
        NativeShader::setDepth(material, true, NEBULA_COMPARE_LEQUAL, false);
        NativeShader::send(material);
    }
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
