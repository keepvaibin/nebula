#include "Game/Screen/WaterCameraFilter.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Screen/MissLayout.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/NebulaArena.hpp"
#include "Game/Render/GameTexture.hpp"
#include <NebulaGpuTypes.h>
#include "Game/Render/TypedDraw.hpp"
#include <nebula_game_api.h>
#include "Game/System/LumaCore.hpp"

namespace {
    // runtime/src/render/materials/water_camera.hlsl: the screen (texture 1) warped by the filter texture (texture 0,
    // turned by `rFilterMtx`) 0.05 screen texels per indirect step, less the water's colour; blended by the filter's
    // alpha where above zero, depth neither tested nor written.
    SiteMaterial filterMaterial(GameTexture* pFilter, GameTexture* pScreen, const Mtx34f& rFilterMtx, const Color8& rWater) {
        SiteMaterial material;
        material.init(NEBULA_SHADER_WATER_CAMERA, SiteMaterial::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        material.mState.alpha_write = 1;
        material.setColor(0, rWater.r, rWater.g, rWater.b, rWater.a);
        for (u32 row = 0; row < 2; row++) {
            for (u32 column = 0; column < 4; column++) {
                material.constants[4 + row * 4 + column] = rFilterMtx.mMtx[row][column];
            }
        }
        material.constants[12] = 0.05f / pScreen->getWidth();
        material.constants[13] = 0.05f / pScreen->getHeight();
        material.setTexture(0, pFilter);
        material.setTexture(1, pScreen);
        return material;
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
    : LiveActor("水中カメラフィルター"), mWaterAlpha(), mWavePhase(), mWaterColor(::sDefaultColor), mScreenTex(), mFilterTex() {
    lumacore::setClass(this, lumacore::kClassId< WaterCameraFilter >);
}

void WaterCameraFilter::init(const TableIter& /*rIter*/) {
    MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_WaterCameraFilter);
    MR::invalidateClipping(this);
    initNerve(GET_NERVE(WaterCameraFilter, WaterCameraFilterNrvAir));
    initScreenTex();
    GameTexture* pFilterTex = new GameTexture(MR::loadTexFromArc("WaterCameraFilter", "WaterCameraFilter.bti"));
    mFilterTex = pFilterTex;
    makeActorAppeared();
}

void WaterCameraFilter::initScreenTex() {
    NebulaArena::CurrentScope restorer(Arenas::scene());
    mScreenTex = GameTexture::createTarget(1.0f, 1.0f);
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

    Frame::takeFrame(mScreenTex->image());
    const SiteMaterial material = loadMaterial();
    TypedDraw site = TypedDraw::eyeSpace(material, cIdentityMtx);
    Vec2f pos;
    pos.x = 0.0f;
    pos.y = 0.0f;
    Vec3f* pPlayerCenter = MR::getPlayerCenterPos();
    MR::calcScreenPosition(&pos, *pPlayerCenter);
    pos.x /= MR::getScreenWidth();
    pos.y /= MR::getScreenHeight();

    f32 halfSize = 0.5f;
    f32 centerU = 1.0f - pos.x;
    f32 centerV = 1.0f - pos.y;

    VertexPT2* vtx = site.vertices< VertexPT2 >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 4);
    {
        *vtx++ = {0.0f, 0.0f, 0.0f, centerU - halfSize, centerV - halfSize, 0.0f, 0.0f};

        *vtx++ = {static_cast< f32 >(static_cast< u16 >(MR::getScreenWidth())), 0.0f, 0.0f, centerU + halfSize, centerV - halfSize, 1.0f, 0.0f};

        *vtx++ = {0.0f, static_cast< f32 >(static_cast< u16 >(MR::getScreenHeight())), 0.0f, centerU - halfSize, centerV + halfSize, 0.0f, 1.0f};

        s32 height = MR::getScreenHeight();
        *vtx++ = {static_cast< f32 >(static_cast< u16 >(MR::getScreenWidth())), static_cast< f32 >(static_cast< u16 >(height)), 0.0f, centerU + halfSize, centerV + halfSize, 1.0f, 1.0f};
    }

    MR::loadProjectionMtx();
    MR::loadViewMtx();
}

SiteMaterial WaterCameraFilter::loadMaterial() const {
    Mtx44 projection;
    s32 height = MR::getScreenHeight();
    NebulaMtxOrtho(projection, 0.0f, height, 0.0f, MR::getScreenWidth(), -10000.0f, 10000.0f);
    Frame::setOrthographic(projection);
    Vec2f screenPos;
    screenPos.x = 0.0f;
    screenPos.y = 0.0f;
    MR::calcScreenPosition(&screenPos, *MR::getPlayerCenterPos());
    screenPos.x /= MR::getScreenWidth();
    screenPos.y /= MR::getScreenHeight();
    Vec3f screenPosNorm;
    MR::calcNormalizedScreenPosition(&screenPosNorm, *MR::getPlayerCenterPos());
    Mtx34f textureMtx;
    textureMtx.identity();
    Pos3f translation;
    translation.identity();
    translation.setTrans(Vec3f(-0.5f, -0.5f, 0.0f));
    Pos3f rotation;
    rotation.identity();
    MR::makeMtxRotate(rotation, 0.0f, 0.0f, mWavePhase);
    MR::multMtx(textureMtx, translation, rotation);
    translation.setTrans(Vec3f(0.5f, 0.5f, 0.0f));
    MR::multMtx(textureMtx, textureMtx, translation);
    return filterMaterial(mFilterTex, mScreenTex, textureMtx,
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
