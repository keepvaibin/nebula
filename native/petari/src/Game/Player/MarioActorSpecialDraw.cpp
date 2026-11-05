#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Util/NativeShader.hpp"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeTarget.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/Player/J3DModelX.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioParts.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Player/MarioSwim.hpp"
#include "Game/Screen/FullScreenBlur.hpp"
#include "Game/Util.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/FurMulti.hpp"
#include <JSystem/JGeometry/TBox.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <new>

void MarioActor::initScreenBox() {
    _B44 = new u8[32];  // the frame read's key
    _B28 = 0.0f;
    _B24 = 0.0f;
    _B30 = 0.0f;
    _B2C = 0.0f;
    _B38 = 0.0f;
    _B34 = 0.0f;
    _B40 = 0.0f;
    _B3C = 0.0f;
}

bool MarioActor::isUseScreenBox() const {
    if (_A08 == 3) {
        return true;
    }

    return !(_A08 - 7);
}

void MarioActor::calcScreenBoxRange() {
    if (!isUseScreenBox()) {
        return;
    }

    TVec3f position;
    position = _B18;
    TVec2f center;
    TVec2f x;
    TVec2f y;
    TVec2f z;
    TVec2f minimum;
    TVec2f maximum;
    MR::calcScreenPosition(&center, position);
    MR::calcScreenPosition(&x, position + TVec3f(80.0f, 0.0f, 0.0f));
    MR::calcScreenPosition(&y, position + TVec3f(0.0f, 80.0f, 0.0f));
    MR::calcScreenPosition(&z, position + TVec3f(0.0f, 0.0f, 80.0f));
    TVec3f radius((x - center).length(), (y - center).length(), (z - center).length());
    f32 range = radius.length();
    minimum = center - TVec2f(range, range);
    maximum = center + TVec2f(range, range);
    _B24 = minimum.x < maximum.x ? minimum.x : maximum.x;
    _B2C = minimum.x > maximum.x ? minimum.x : maximum.x;
    _B28 = minimum.y < maximum.y ? minimum.y : maximum.y;
    _B30 = minimum.y > maximum.y ? minimum.y : maximum.y;
    s16 left = static_cast< s32 >(mScreenBoxMin.x);
    s16 top = static_cast< s32 >(mScreenBoxMin.y);
    s16 width = static_cast< s32 >(mScreenBoxMax.x) - left + 1;
    s16 height = static_cast< s32 >(mScreenBoxMax.y) - top + 1;

    if (!width) {
        width = 2;
    }

    if (!height) {
        height = 2;
    }

    _B34 = left;
    _B38 = top;
    _B3C = width;
    _B40 = height;
    TBox2f capture(_B34, _B38, _B34 + _B3C, _B38 + _B40);
    s32 screenHeight = MR::getScreenHeight();
    TBox2f screen(0.0f, 0.0f, MR::getScreenWidth(), screenHeight);
    TBox2f clipped(capture);

    if (!clipped.intersect(screen)) {
        clipped.i.zero();
        clipped.f.zero();
    }

    _B34 = clipped.i.x;
    _B38 = clipped.i.y;
    _B3C = clipped.getWidth();
    _B40 = clipped.getHeight();
}

void MarioActor::captureScreenBox() const {
    if (!isUseScreenBox()) {
        return;
    }

    if (_B3C < 1.0f || _B40 < 1.0f) {
        return;
    }

    // The model's own target, the frame's size: its alpha is what the box composites by.
    J3DNativeTarget::begin(_B44, 1.0f, 1.0f, 0);
    J3DPass::writeColor(true);
    J3DPass::writeAlpha(false);
}

void MarioActor::writeBackScreenBox() const {
    if (!isUseScreenBox()) {
        return;
    }

    if (_B3C < 1.0f || _B40 < 1.0f) {
        return;
    }

    J3DNativeTarget::end(_B44, NEBULA_TARGET_RGBA);

    J3DNativeTexObj texture;
    TVec2f minimum;
    TVec2f maximum;
    const f32 screenWidth = MR::getScreenWidth();
    const f32 screenHeight = MR::getScreenHeight();
    const f32 u0 = static_cast< s32 >(_B34) / screenWidth;
    const f32 v0 = static_cast< s32 >(_B38) / screenHeight;
    const f32 u1 = (static_cast< s32 >(_B34) + static_cast< s32 >(_B3C)) / screenWidth;
    const f32 v1 = (static_cast< s32 >(_B38) + static_cast< s32 >(_B40)) / screenHeight;
    J3DNativeTexture::init(&texture, _B44, JUTTexture::cAuthoredAcross, JUTTexture::cAuthoredDown, NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP);
    minimum = mScreenBoxPos;
    maximum = mScreenBoxPos + mScreenBoxSize;
    NebulaNativeMaterial material;
    TDDraw::setupNative(&material, 1, 0, 2);
    NativeShader::setBlend(material, NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_INV_SRC_ALPHA);
    NativeShader::setDepth(material, true, NEBULA_COMPARE_ALWAYS, true);
    NativeShader::setTexObj(material, 0, &texture);
    NativeShader::send(material);
    J3DNativeDraw::begin(NEBULA_PRIMITIVE_QUADS, 4, NEBULA_SHAPE_TEXCOORD0);
    J3DNativeDraw::position(minimum.x, minimum.y, 0.9999f);
    J3DNativeDraw::texCoord(u0, v0);
    J3DNativeDraw::position(maximum.x, minimum.y, 0.9999f);
    J3DNativeDraw::texCoord(u1, v0);
    J3DNativeDraw::position(maximum.x, maximum.y, 0.9999f);
    J3DNativeDraw::texCoord(u1, v1);
    J3DNativeDraw::position(minimum.x, maximum.y, 0.9999f);
    J3DNativeDraw::texCoord(u0, v1);
    TDDraw::close();
}

void MarioActor::calc1stPersonView() {
    f32 length = (mCamPos - mPosition).length();
    f32 minVal = 300.0f;
    f32 maxVal = 1000.0f;
    f32 val;

    if (length > maxVal) {
        val = 1.0f;
    } else if (length < minVal) {
        val = 0.0f;
    } else {
        val = 1.0f - (maxVal - length) / (maxVal - minVal);
    }

    u8 compareVal = (s32)(255.0f * val);

    if (compareVal == 0) {
        _1A1 = true;
        hideBeeFur();
    } else {
        _1A1 = false;
        updateAlphaList(compareVal);
    }
}

void MarioActor::hideBeeFur() {
    if (mMario->isPlayerModeBee()) {
        _9E8->kill();
        _9EC->offDraw(-1);
    }

    if (getCarrySensor() != nullptr) {
        MR::hideModel(getCarrySensor()->mHost);
    }

    if (mMario->isPlayerModeInvincible()) {
        _A6E = 0;
        stopEffect("\226\263\223\107\222\206" /* 無敵中 */);
    }
}

void MarioActor::calcFogLighting() {
    Color8 fog(240, 16, 80, 0);

    if (!mMario->isStatusActive(MarioStatus_Paralyze) && isNeedDamageFog()) {
        f32 phase = ((_1A8 + 31) & 63) / 63.0f;

        if (phase > 0.5f) {
            phase = 1.0f - phase;
        }

        f32 wave = 2.0f * phase;
        f32 low = getConst().getTable()->mDamageFogLow;
        _1A4 = (low + wave * (getConst().getTable()->mDamageFogHigh - getConst().getTable()->mDamageFogLow)) / 255.0f;
    } else if (_1AA) {
        fog.mRGBA = _1B0.mRGBA;

        if (_1B5) {
            fog.g = (s32)(255.0f * MR::sin(JGeometry::TUtil< f32 >::PI() * (_1AA / 20.0f)));
        }

        _1A4 = _1AC * MR::sin((_1AA / static_cast< f32 >(getConst().getTable()->mStarPieceFogTime)) * JGeometry::TUtil< f32 >::PI());
    } else if (mMario->_434) {
        fog.set(255, 255, 0, 0);
        _1A4 = 0.8f * MR::sin(((mMario->_434 / static_cast< f32 >(getConst().getTable()->mItemDashTimer)) * JGeometry::TUtil< f32 >::PI()) / 2.0f);
    } else {
        resetFog();
    }

    Color8 ambient = *MR::getLightAmbientColor(this);
    updateLightList(ambient, Color8(255, 255, 255, 255), fog, _1A4);
}

void MarioActor::resetFog() {
    Color8 color(0, 0, 0, 0);
    _1A8 = 0;
    _1A4 = 0.0f;
    _1AA = 0;
}

void MarioActor::updateAlphaList(u8 alpha) {
    mAlphaTint.kind = MarioModelTint::Kind_Alpha;
    mAlphaTint.color0.r = mAlphaTint.color0.g = mAlphaTint.color0.b = 0;
    mAlphaTint.color0.a = alpha;
}

void MarioActor::updateSimpleAlphaList(u8 alpha) {
    mAlphaTint.kind = MarioModelTint::Kind_ConstantAlpha;
    mAlphaTint.alpha = alpha;
}

void MarioActor::updateReflectAlphaList(u8 alpha) {
    mAlphaTint.kind = MarioModelTint::Kind_ReflectAlpha;
    mAlphaTint.color0.r = mAlphaTint.color0.g = mAlphaTint.color0.b = 0;
    mAlphaTint.color0.a = alpha;
}

void MarioActor::updateLightList(const Color8& rAmbient, const Color8& rMaterial, const Color8& rFog, f32 strength) {
    mLightTint.kind = MarioModelTint::Kind_Light;
    mLightTint.color0 = rAmbient.mRGBA;
    mLightTint.color1 = rMaterial.mRGBA;
    mLightTint.hasFog = !MR::isNearZero(strength);
    if (mLightTint.hasFog) {
        mLightTint.fogNear = MR::getNearZ();
        mLightTint.fogFar = MR::getFarZ();
        mLightTint.fogColor = rFog.mRGBA;
        MR::calcFogStartEnd(mPosition, strength, &mLightTint.fogStart, &mLightTint.fogEnd);
    }
}

void MarioActor::createRainbowMesh() {
    for (u32 alpha = 0; alpha < 8; alpha++) {
        for (u32 index = 0; index < 8; index++) {
            NebulaColor8 color = {0, 0, 0, 0};

            if (index & 1) {
                color.r = 255;
            }

            if (index & 2) {
                color.g = 255;
            }

            if (index & 4) {
                color.b = 255;
            }

            if (!index) {
                color.r = 64;
                color.g = 64;
                color.b = 64;
            }

            color.a = 16 + (alpha + 1) * 24;
            mGhostTints[alpha * 8 + index].kind = MarioModelTint::Kind_Ghost;
            mGhostTints[alpha * 8 + index].color0 = color;
        }
    }
}

void MarioActor::drawScreenBlend() const {
    if (mMario->_97C != nullptr && mMario->_97C->getBlurOffset() != 0.0f) {
        MR::drawFullScreenBlur(mMario->_97C->getBlurOffset());
    }

    if (_1C6) {
        MR::drawFullScreenBlur(_1C8, _1CC, _1D0, _1D1);
    }

    if (mBeeWallWalk) {
        TVec2f position;
        TVec2f opposite;
        TVec2f minimum;
        TVec2f maximum;
        TDDraw::project2D(&position, mMario->mShadowPos);
        opposite = position + TVec2f(100.0f, 100.0f);
        position = position + TVec2f(-100.0f, -100.0f);

        if (position.x >= opposite.x) {
            minimum.x = opposite.x;
            maximum.x = position.x;
        } else {
            minimum.x = position.x;
            maximum.x = opposite.x;
        }

        if (position.y >= opposite.y) {
            minimum.y = opposite.y;
            maximum.y = position.y;
        } else {
            minimum.y = position.y;
            maximum.y = opposite.y;
        }

        position = minimum + TVec2f(-10.0f, -10.0f);
        opposite = maximum + TVec2f(10.0f, 10.0f);
        // Where the wall's shadow was marked: darkened, the mark going with it.
        NebulaNativeMaterial material;
        TDDraw::setupNative(&material, 0, 0, 2);
        NativeShader::setBlend(material, NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_INV_SRC_ALPHA);
        NativeShader::send(material);
        J3DPass::stencil(NEBULA_STENCIL_IF_SET, NEBULA_STENCIL_ZERO);
        TDDraw::drawFillBox(position, opposite, 0x00000040);
        J3DPass::stencil(NEBULA_STENCIL_OFF, NEBULA_STENCIL_KEEP);
        MR::loadProjectionMtx();
        MR::loadViewMtx();
    }

    if (_BC4) {
        u32 color = 0xFF000080;

        if (mMario->_1C._3) {
            color = 0x00004020;
        }

        NebulaNativeMaterial material;
        TDDraw::setupNative(&material, 0, 1, 2);
        NativeShader::send(material);
        TDDraw::drawFillBox(TVec3f(0.0f, 0.0f, 0.0f),
                            TVec3f(static_cast< f32 >(MR::getScreenWidth()), static_cast< f32 >(MR::getScreenHeight()), 0.0f), color);
        TDDraw::close();
    }
}

void MarioActor::updateRandomTexture(f32 distance) {
    _B88 = 1 - _B88;
    u8* pixel;
    u8* image = mMaskTextures[_B88]->mImage;
    distance = 1.0f - distance / 1000.0f;
    f32 probability = MR::clamp(distance, 0.0f, 1.0f);

    // LA8 texels in rows: intensity 0, the alpha in 16 levels (level * 17).
    for (u32 y = 0; y < 8; y++) {
        pixel = image + y * 16;

        for (u32 x = 0; x < 8; x++) {
            s32 alpha = pixel[1] / 17;

            if (MR::getRandom() < probability) {
                alpha += 4;
            } else {
                alpha--;
            }

            s32 clamped = alpha < 0 ? 0 : alpha > 15 ? 15 : alpha;
            pixel[0] = 0;
            pixel[1] = static_cast< u8 >(clamped * 17);
            pixel += 2;
        }
    }

    J3DNativeTexture::registerLinear(image, NEBULA_TEXTURE_LINEAR_LA8, 8, 8, 1);
}

namespace {
    inline TVec3f subtractOffset(const TVec3f& rBase, const TVec3f& rOffset) {
        const TVec3f* pOffset = &rOffset;
        return rBase - *pOffset;
    }
};  // namespace

void MarioActor::drawWallShade(const TVec3f& rPosition, const TVec3f& rNormal, f32 unused) const {
    f32 radius = 100.0f;
    NebulaNativeMaterial material;
    TDDraw::setupNative(&material, 1, 1, 0);
    material.shader = NEBULA_SHADER_TEX_VERTEX_ALPHA;
    material.constants[0] = 0.0f;
    NativeShader::setDepth(material, true, NEBULA_COMPARE_GREATER, false);
    NativeShader::setTexture(material, 0, mMaskTextures[_B88]);
    NativeShader::send(material);
    Mtx rotation;
    TVec3f tangent(0.0f, rNormal.z, -rNormal.y);

    if (MR::isNearZero(tangent)) {
        tangent.set< f32 >(rNormal.z, 0.0f, rNormal.x);
    }

    MR::normalizeOrZero(&tangent);
    NebulaMtxRotAxisRad(rotation, &rNormal, 0.3926991f);
    J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLE_FAN, 18, NEBULA_SHAPE_COLOR0 | NEBULA_SHAPE_TEXCOORD0);
    J3DNativeDraw::position(rPosition.x - 5.0f * rNormal.x, rPosition.y - 5.0f * rNormal.y, rPosition.z - 5.0f * rNormal.z);
    J3DNativeDraw::color(128);
    J3DNativeDraw::texCoord(0.0f, 0.0f);

    for (u32 i = 0; i <= 16; i++) {
        f32 angle = 2.0f * ((i / 16.0f) * JGeometry::TUtil< f32 >::PI());
        TVec3f point = subtractOffset(rPosition, rNormal * 5.0f) + tangent * radius;
        J3DNativeDraw::position(point.x, point.y, point.z);
        J3DNativeDraw::color(1);
        J3DNativeDraw::texCoord(10.0f * JMACosRadian(angle), 10.0f * MR::sin(angle));
        NebulaMtxMultVecSR(rotation, &tangent, &tangent);
    }
}

void MarioActor::drawSpinInhibit() const {
}

namespace {
    // runtime/src/render/materials/frame_ripple.hlsl over the whole screen: the frame's rows moved by the wave.
    void drawFrameRipple(const void* pFrame, f32 amplitude, f32 phase, f32 perRow, f32 kind) {
        J3DNativeTexObj texture;
        const u16 height = MR::getScreenHeight();
        NebulaNativeMaterial material;
        TDDraw::setupNative(&material, 1, 0, 2);
        NativeShader::init(material, NEBULA_SHADER_FRAME_RIPPLE, NativeShader::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        J3DNativeTexture::init(&texture, const_cast< void* >(pFrame), JUTTexture::cAuthoredAcross, JUTTexture::cAuthoredDown, NEBULA_WRAP_CLAMP,
                               NEBULA_WRAP_CLAMP);
        J3DNativeTarget::take(pFrame);
        material.alpha_write = 1;
        NativeShader::setTexObj(material, 0, &texture);
        material.constants[0] = amplitude;
        material.constants[1] = height;
        material.constants[2] = MR::getScreenWidth();
        material.constants[3] = kind;
        material.constants[4] = phase;
        material.constants[5] = perRow;
        NativeShader::send(material);
        J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 4, NEBULA_SHAPE_TEXCOORD0);
        J3DNativeDraw::position(0.0f, height, 0.0f);
        J3DNativeDraw::texCoord(0.0f, 1.0f);
        J3DNativeDraw::position(0.0f, 0.0f, 0.0f);
        J3DNativeDraw::texCoord(0.0f, 0.0f);
        J3DNativeDraw::position(MR::getScreenWidth(), height, 0.0f);
        J3DNativeDraw::texCoord(1.0f, 1.0f);
        J3DNativeDraw::position(MR::getScreenWidth(), 0.0f, 0.0f);
        J3DNativeDraw::texCoord(1.0f, 0.0f);
        J3DPass::writeAlpha(false);
    }
}  // namespace

void MarioActor::drawColdWaterDamage() const {
    f32 frequency = 89.0f * MR::sin((TWO_PI * mMario->mSwim->mColdWaterDamageInterval) / 120.0f);

    if (frequency > 0.0f) {
        frequency = 0.0f;
    }

    drawFrameRipple(_1D8, 5.0f, _37C, frequency, 0.0f);
}

void MarioActor::setRasterScroll(s32 i1, s32 i2, s32 i3) {
    _1E8 = i2;
    _1E4 = 0.9f * _1E4 + 0.1f * i1;
    _1EC = i3;
    _1E2 = 1;
}

void MarioActor::updateRasterScroll() {
    if (_1E2) {
        _1E2 = 0;
        return;
    }

    _1E4 *= 0.9f;

    if (_1E4 < 1.0f) {
        _1E8 = 0;
        _1E4 = 0.0f;
    }

    if (_1EC < 1.0f) {
        _1EC = 0.0f;
    }
}

void MarioActor::drawRasterScroll(f32 amplitude, s16 period, f32 wavelength) const {
    if (wavelength == 0.0f) {
        return;
    }

    if (period == 0) {
        return;
    }

    const f32 phase = (_37C * JMath::TAngleConstant_< f32 >::RADIAN_DEG360()) / period;
    drawFrameRipple(_1D8, amplitude, phase, TWO_PI / wavelength, 1.0f);
}

void MarioActor::drawMosaic() const {
}

void MarioActor::drawLifeUp() const {
}

void MarioActor::calcSpinEffect() {
    f32 baseRadius = 30.0f;
    f32 radiusScale = 5.8f;
    _6D4 = 0.0f;
    _6D8 = 0.0f;

    if (selectAction("\203\130\203\163\203\223\211\361\225\234\203\107\203\164\203\106\203\116\203\147" /* スピン回復エフェクト */) != 1) {
        return;
    }

    if (!_945) {
        return;
    }

    if (_944) {
        return;
    }

    if (!_946) {
        return;
    }

    s32 frame = _946 - 9;

    if (frame <= 0 || frame > 15) {
        _6D4 = 0.0f;
        return;
    }

    if (frame < 10) {
        _6D4 = baseRadius + (frame / 10.0f) * (_945 * radiusScale);
        _6D8 = 120.0f;
        return;
    }

    _6D4 = baseRadius + _945 * radiusScale;
    _6D8 = 120.0f;
}

void MarioActor::drawSpinEffect() const {
    if (_6D4 == 0.0f) {
        return;
    }

    if (!isEnableNerveChange()) {
        return;
    }

    NebulaNativeMaterial material;
    TDDraw::setupNative(&material, 0, 2, 0);
    NativeShader::send(material);
    Mtx rotation;
    TVec3f center = _2A0 + mMario->mHeadVec * _6D8;
    TVec3f direction(mMario->mFrontVec);
    TVec3f previous;
    NebulaMtxRotAxisRad(rotation, &mMario->mHeadVec, 0.09817477f);
    J3DPass::lineWidth(6.0f);

    for (u32 i = 0; i <= 64; i++) {
        TVec3f point(direction);
        point.setLength(_6D4);
        point += mMario->mHeadVec * MR::getRandom() * 10.0f;

        if (i) {
            TDDraw::drawLine(center + previous, center + point, 0xFFFFFF20);
        }

        previous = point;
        NebulaMtxMultVecSR(rotation, &direction, &direction);
    }

    J3DPass::lineWidth(3.0f);

    for (u32 i = 0; i <= 64; i++) {
        TVec3f point(direction);
        point.setLength(_6D4);
        point += mMario->mHeadVec * MR::getRandom() * 10.0f;
        f32 wave = MR::sin((((_37C + i) & 31) / 32.0f) * JGeometry::TUtil< f32 >::PI());
        f32 square = wave * wave;
        u32 alpha = 160.0f * (square * square);

        if (i) {
            TDDraw::drawLine(center + previous, center + point, alpha | 0x40FF00 | (alpha << 24));
        }

        previous = point;
        NebulaMtxMultVecSR(rotation, &direction, &direction);
    }

    TDDraw::close();
}

void MarioActor::drawSphereMask() const {
    if (!mMario->isVisibleRecoveryWarpBubble()) {
        return;
    }

    NebulaNativeMaterial material;
    TDDraw::setupNative(&material, 0, 2, 0);
    J3DNativeView::setZScaleOffset(1.0f, 0.00001f);
    material.cull = NEBULA_CULL_FRONT;
    NativeShader::setDepth(material, true, NEBULA_COMPARE_GREATER, true);
    NativeShader::send(material);
    TVec3f position;
    getRealPos("Spine1", &position);
    TDDraw::drawSphere3D(position, 180.0f, 0xFFFFFF30, 16);
    J3DNativeView::setZScaleOffset(1.0f, 0.0f);
}

void MarioActor::initDarkMask() {
    for (u32 texture = 0; texture < 2; texture++) {
        u8* image = mMaskTextures[texture]->mImage;

        for (u32 i = 0; i < 8 * 8 * 2; i++) {
            image[i] = 0;
        }
        J3DNativeTexture::registerLinear(image, NEBULA_TEXTURE_LINEAR_LA8, 8, 8, 1);
    }

    _1C3 = true;
    _1C4 = 0;
}

void MarioActor::updateDarkMask(u16 unused) {
    if (!_1C3) {
        return;
    }

    u8* previous = mMaskTextures[_B88]->mImage;
    _B88 = 1 - _B88;
    u8* image = mMaskTextures[_B88]->mImage;
    u8 x = MR::getRandom(0, 8);
    u8 y = MR::getRandom(0, 8);
    s32 index = 2 * (x + y * 8);
    image[index] = 0;
    image[index + 1] = 255;
    previous[index] = 0;
    previous[index + 1] = 255;
    J3DNativeTexture::registerLinear(image, NEBULA_TEXTURE_LINEAR_LA8, 8, 8, 1);
    _1C4++;
}

bool MarioActor::drawDarkMask() const {
    if (!_1C3) {
        return false;
    }

    NebulaNativeMaterial material;
    TDDraw::setupNative(&material, 1, 0, 2);
    material.color_write = 0;
    material.alpha_write = 0;
    material.constants[4] = 1.0f;
    NativeShader::setTexture(material, 0, mMaskTextures[_B88]);
    NativeShader::send(material);
    J3DPass::stencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ADD);
    TVec3f spine;
    TVec2f center;
    TVec2f screenX;
    TVec2f screenY;
    TVec2f origin;
    TVec2f minimum;
    TVec2f maximum;
    getRealPos("Spine1", &spine);
    f32 radius = _1C4;

    if (radius >= 240.0f) {
        radius = 240.0f;
    }

    TVec3f position(mPosition);
    TDDraw::project2D(&center, position);
    TDDraw::project2D(&screenX, position + mCamDirX * radius);
    TDDraw::project2D(&screenY, position + mCamDirY * radius);
    u16 width = ((static_cast< u32 >((screenX - center).length()) + 3) & ~3) + 4;
    u16 height = ((static_cast< u32 >((screenY - center).length()) + 3) & ~3) + 4;
    TDDraw::project2D(&origin, position);
    origin.x -= width / 2;
    origin.y -= width / 2;
    minimum = origin;
    maximum = origin + TVec2f(width, height);
    J3DNativeDraw::begin(NEBULA_PRIMITIVE_QUADS, 4, NEBULA_SHAPE_TEXCOORD0);
    J3DNativeDraw::position(minimum.x, minimum.y, 0.0f);
    J3DNativeDraw::texCoord(0.0f, 0.0f);
    J3DNativeDraw::position(maximum.x, minimum.y, 0.0f);
    J3DNativeDraw::texCoord(4.0f, 0.0f);
    J3DNativeDraw::position(maximum.x, maximum.y, 0.0f);
    J3DNativeDraw::texCoord(4.0f, 4.0f);
    J3DNativeDraw::position(minimum.x, maximum.y, 0.0f);
    J3DNativeDraw::texCoord(0.0f, 4.0f);
    J3DPass::stencil(NEBULA_STENCIL_OFF, NEBULA_STENCIL_KEEP);
    J3DPass::writeColor(true);
    TDDraw::close();
    return true;
}

void MarioActor::showBeeFur() {
    if (mMario->isPlayerModeBee()) {
        _9E8->appear();
        _9EC->onDraw(-1);
    }

    if (getCarrySensor() != nullptr) {
        MR::showModel(getCarrySensor()->mHost);
    }

    if (mMario->isPlayerModeInvincible()) {
        MR::showJoint(getJ3DModel(), "Face0");
        _A6E = 2;
        playEffect("\226\263\223\107\222\206" /* 無敵中 */);
    }
}

f32 MarioState::getBlurOffset() const {
    return 0.0f;
}
