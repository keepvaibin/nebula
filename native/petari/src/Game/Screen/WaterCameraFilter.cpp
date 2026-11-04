#include "Game/Screen/WaterCameraFilter.hpp"
#include "JSystem/J3DGraphBase/J3DNativeTarget.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Screen/MissLayout.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>
#include "Game/System/NebulaArena.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include <NebulaGpuTypes.h>
#include "Game/Util/NativeShader.hpp"
#include <nebula_game_api.h>

namespace {
    // runtime/src/render/materials/water_camera.hlsl: the screen (texture 1) warped by the filter texture (texture 0,
    // turned by `rFilterMtx`) 0.05 screen texels per indirect step, less the water's colour; blended by the filter's
    // alpha where above zero, depth neither tested nor written.
    void sendFilterMaterial(JUTTexture* pFilter, JUTTexture* pScreen, const TMtx34f& rFilterMtx, const Color8& rWater) {
        NebulaNativeMaterial material;
        NativeShader::init(material, NEBULA_SHADER_WATER_CAMERA, NativeShader::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        material.alpha_write = 1;
        NativeShader::setColor(material, 0, rWater.r, rWater.g, rWater.b, rWater.a);
        for (u32 row = 0; row < 2; row++) {
            for (u32 column = 0; column < 4; column++) {
                material.constants[4 + row * 4 + column] = rFilterMtx.mMtx[row][column];
            }
        }
        material.constants[12] = 0.05f / pScreen->getWidth();
        material.constants[13] = 0.05f / pScreen->getHeight();
        NativeShader::setTexture(material, 0, pFilter);
        NativeShader::setTexture(material, 1, pScreen);
        NativeShader::send(material);
    }
}  // namespace

namespace NrvWaterCameraFilter {
    NEW_NERVE(WaterCameraFilterNrvAir, WaterCameraFilter, Air);
    NEW_NERVE(WaterCameraFilterNrvAirToWater, WaterCameraFilter, AirToWater);
    NEW_NERVE(WaterCameraFilterNrvWater, WaterCameraFilter, Water);
    NEW_NERVE(WaterCameraFilterNrvWaterToAir, WaterCameraFilter, WaterToAir);
};  // namespace NrvWaterCameraFilter


static NebulaColor8 sDefaultColor;

WaterCameraFilter::WaterCameraFilter()
    : LiveActor("\220\205\222\206\203\112\203\201\203\211\203\164\203\102\203\213\203\136\201\133" /* 水中カメラフィルター */), mWaterAlpha(), mWavePhase(), mWaterColor(::sDefaultColor), mScreenTex(), mFilterTex() {
}

void WaterCameraFilter::init(const JMapInfoIter& rIter) {
    MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_WaterCameraFilter);
    MR::invalidateClipping(this);
    initNerve(GET_NERVE(WaterCameraFilter, WaterCameraFilterNrvAir));
    initScreenTex();
    JUTTexture* pFilterTex = new JUTTexture(MR::loadTexFromArc("WaterCameraFilter.arc", "WaterCameraFilter.bti"));
    mFilterTex = pFilterTex;
    makeActorAppeared();
}

void WaterCameraFilter::initScreenTex() {
    MR::CurrentHeapRestorer restorer(MR::getSceneHeap());
    mScreenTex = JUTTexture::createTarget(1.0f, 1.0f);
}

void WaterCameraFilter::exeAirToWater() {
    if (!MR::isCameraInWater()) {
        setNerve(GET_NERVE(WaterCameraFilter, WaterCameraFilterNrvWaterToAir));
    } else {
        mWaterAlpha += 20.0f;
        if (MR::isDemoActive() || mWaterAlpha >= 255.0f) {
            mWaterAlpha = 255.0f;
            setNerve(GET_NERVE(WaterCameraFilter, WaterCameraFilterNrvWater));
        }
    }
}

void WaterCameraFilter::exeWaterToAir() {
    if (MR::isCameraInWater()) {
        setNerve(GET_NERVE(WaterCameraFilter, WaterCameraFilterNrvAirToWater));
    } else {
        mWaterAlpha -= 20.0f;
        if (MR::isDemoActive() || mWaterAlpha <= 0.0f) {
            mWaterAlpha = 0.0f;
            setNerve(GET_NERVE(WaterCameraFilter, WaterCameraFilterNrvAir));
        }
    }
}

static NebulaColor8 sWaterColor = {0x32, 0x32, 0, 0};

void WaterCameraFilter::control() {
    if (MR::isCameraInWater()) {
        mWavePhase += 0.5f;

        if (mWavePhase > 360.0f) {
            mWavePhase -= 360.0f;
        }

        f32 depth = MR::clamp(WaterAreaFunction::getCameraWaterDepth(), 0.0f, 3000.0f);
        Color8 color(MR::lerp(::sDefaultColor, ::sWaterColor, depth / 3000.0f));
        mWaterColor.mRGBA = color.mRGBA;
    }
}

void WaterCameraFilter::draw() const {
    if (isNerve(GET_NERVE(WaterCameraFilter, WaterCameraFilterNrvAir))) {
        return;
    }

    J3DNativeTarget::take(mScreenTex->mImage);
    loadMaterial();
    TVec2f pos;
    pos.x = 0.0f;
    pos.y = 0.0f;
    TVec3f* pPlayerCenter = MR::getPlayerCenterPos();
    MR::calcScreenPosition(&pos, *pPlayerCenter);
    pos.x /= MR::getScreenWidth();
    pos.y /= MR::getScreenHeight();

    f32 halfSize = 0.5f;
    f32 centerU = 1.0f - pos.x;
    f32 centerV = 1.0f - pos.y;

    J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 4, NEBULA_SHAPE_TEXCOORD0 | NEBULA_SHAPE_TEXCOORD0 << 1);
    {
        J3DNativeDraw::position(0.0f, 0.0f, 0.0f);
        J3DNativeDraw::texCoord(centerU - halfSize, centerV - halfSize);
        J3DNativeDraw::texCoord(0.0f, 0.0f);

        J3DNativeDraw::position(static_cast< f32 >(static_cast< u16 >(MR::getScreenWidth())), 0.0f, 0.0f);
        J3DNativeDraw::texCoord(centerU + halfSize, centerV - halfSize);
        J3DNativeDraw::texCoord(1.0f, 0.0f);

        J3DNativeDraw::position(0.0f, static_cast< f32 >(static_cast< u16 >(MR::getScreenHeight())), 0.0f);
        J3DNativeDraw::texCoord(centerU - halfSize, centerV + halfSize);
        J3DNativeDraw::texCoord(0.0f, 1.0f);

        s32 height = MR::getScreenHeight();
        J3DNativeDraw::position(static_cast< f32 >(static_cast< u16 >(MR::getScreenWidth())), static_cast< f32 >(static_cast< u16 >(height)), 0.0f);
        J3DNativeDraw::texCoord(centerU + halfSize, centerV + halfSize);
        J3DNativeDraw::texCoord(1.0f, 1.0f);
    }

    J3DPass::writeAlpha(false);
    MR::loadProjectionMtx();
    MR::loadViewMtx();
}

void WaterCameraFilter::loadMaterial() const {
    Mtx44 projection;
    s32 height = MR::getScreenHeight();
    NebulaMtxOrtho(projection, 0.0f, height, 0.0f, MR::getScreenWidth(), -10000.0f, 10000.0f);
    J3DNativeView::setOrthographic(projection);
    TMtx34f modelView;
    modelView.identity();
    J3DNativeShape::setPosition(0, modelView);
    J3DPass::positionMatrix(0);
    TVec2f screenPos;
    screenPos.x = 0.0f;
    screenPos.y = 0.0f;
    MR::calcScreenPosition(&screenPos, *MR::getPlayerCenterPos());
    screenPos.x /= MR::getScreenWidth();
    screenPos.y /= MR::getScreenHeight();
    TVec3f screenPosNorm;
    MR::calcNormalizedScreenPosition(&screenPosNorm, *MR::getPlayerCenterPos());
    TMtx34f textureMtx;
    textureMtx.identity();
    TPos3f translation;
    translation.identity();
    translation.setTrans(TVec3f(-0.5f, -0.5f, 0.0f));
    TPos3f rotation;
    rotation.identity();
    MR::makeMtxRotate(rotation, 0.0f, 0.0f, mWavePhase);
    MR::multMtx(textureMtx, translation, rotation);
    translation.setTrans(TVec3f(0.5f, 0.5f, 0.0f));
    MR::multMtx(textureMtx, textureMtx, translation);
    sendFilterMaterial(mFilterTex, mScreenTex, textureMtx,
                       Color8(mWaterColor.r, mWaterColor.g, mWaterColor.b, (s32)(mWaterAlpha)));
}

void WaterCameraFilter::exeWater() {
    if (!MR::isCameraInWater()) {
        setNerve(GET_NERVE(WaterCameraFilter, WaterCameraFilterNrvWaterToAir));
    }
}

void WaterCameraFilter::exeAir() {
    if (MR::isCameraInWater()) {
        setNerve(GET_NERVE(WaterCameraFilter, WaterCameraFilterNrvAirToWater));
    }
}
