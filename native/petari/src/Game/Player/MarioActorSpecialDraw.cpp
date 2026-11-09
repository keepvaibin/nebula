#include "Game/LiveActor/HitSensor.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/RenderTexture.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Player/PlayerModel.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioParts.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Player/MarioSwim.hpp"
#include "Game/Screen/FullScreenBlur.hpp"
#include "Game/Util.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/FurMulti.hpp"
#include <nebula_geometry.hpp>
#include "Game/Render/GameTexture.hpp"
#include <new>

void MarioActor::initScreenBox() {
    mScreenBoxKey = new u8[32];  // the frame read's key
    mScreenBoxMin.y = 0.0f;
    mScreenBoxMin.x = 0.0f;
    mScreenBoxMax.y = 0.0f;
    mScreenBoxMax.x = 0.0f;
    mScreenBoxPos.y = 0.0f;
    mScreenBoxPos.x = 0.0f;
    mScreenBoxSize.y = 0.0f;
    mScreenBoxSize.x = 0.0f;
}

bool MarioActor::isUseScreenBox() const {
    if (mShadowMode == 3) {
        return true;
    }

    return !(mShadowMode - 7);
}

void MarioActor::calcScreenBoxRange() {
    if (!isUseScreenBox()) {
        return;
    }

    Vec3f position;
    position = mReflectPos;
    Vec2f center;
    Vec2f x;
    Vec2f y;
    Vec2f z;
    Vec2f minimum;
    Vec2f maximum;
    MR::calcScreenPosition(&center, position);
    MR::calcScreenPosition(&x, position + Vec3f(80.0f, 0.0f, 0.0f));
    MR::calcScreenPosition(&y, position + Vec3f(0.0f, 80.0f, 0.0f));
    MR::calcScreenPosition(&z, position + Vec3f(0.0f, 0.0f, 80.0f));
    Vec3f radius((x - center).length(), (y - center).length(), (z - center).length());
    f32 range = radius.length();
    minimum = center - Vec2f(range, range);
    maximum = center + Vec2f(range, range);
    mScreenBoxMin.x = minimum.x < maximum.x ? minimum.x : maximum.x;
    mScreenBoxMax.x = minimum.x > maximum.x ? minimum.x : maximum.x;
    mScreenBoxMin.y = minimum.y < maximum.y ? minimum.y : maximum.y;
    mScreenBoxMax.y = minimum.y > maximum.y ? minimum.y : maximum.y;
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

    mScreenBoxPos.x = left;
    mScreenBoxPos.y = top;
    mScreenBoxSize.x = width;
    mScreenBoxSize.y = height;
    Box2f capture(mScreenBoxPos.x, mScreenBoxPos.y, mScreenBoxPos.x + mScreenBoxSize.x, mScreenBoxPos.y + mScreenBoxSize.y);
    s32 screenHeight = MR::getScreenHeight();
    Box2f screen(0.0f, 0.0f, MR::getScreenWidth(), screenHeight);
    Box2f clipped(capture);

    if (!clipped.intersect(screen)) {
        clipped.i.zero();
        clipped.f.zero();
    }

    mScreenBoxPos.x = clipped.i.x;
    mScreenBoxPos.y = clipped.i.y;
    mScreenBoxSize.x = clipped.getWidth();
    mScreenBoxSize.y = clipped.getHeight();
}

void MarioActor::captureScreenBox() const {
    if (!isUseScreenBox()) {
        return;
    }

    if (mScreenBoxSize.x < 1.0f || mScreenBoxSize.y < 1.0f) {
        return;
    }

    // The model's own target, the frame's size: its alpha is what the box composites by.
    Frame::targetBegin(mScreenBoxKey, 1.0f, 1.0f, 0);
    Frame::setPassAlpha(false);
}

void MarioActor::writeBackScreenBox() const {
    if (!isUseScreenBox()) {
        return;
    }

    if (mScreenBoxSize.x < 1.0f || mScreenBoxSize.y < 1.0f) {
        return;
    }

    Frame::targetEnd(mScreenBoxKey, NEBULA_TARGET_RGBA);

    SiteTexture texture;
    Vec2f minimum;
    Vec2f maximum;
    const f32 screenWidth = MR::getScreenWidth();
    const f32 screenHeight = MR::getScreenHeight();
    const f32 u0 = static_cast< s32 >(mScreenBoxPos.x) / screenWidth;
    const f32 v0 = static_cast< s32 >(mScreenBoxPos.y) / screenHeight;
    const f32 u1 = (static_cast< s32 >(mScreenBoxPos.x) + static_cast< s32 >(mScreenBoxSize.x)) / screenWidth;
    const f32 v1 = (static_cast< s32 >(mScreenBoxPos.y) + static_cast< s32 >(mScreenBoxSize.y)) / screenHeight;
    SiteTextures::init(&texture, mScreenBoxKey, NebulaFrameClock::cEffectTexelsAcross, NebulaFrameClock::cScreenHeight, NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP);
    minimum = mScreenBoxPos;
    maximum = mScreenBoxPos + mScreenBoxSize;
    SiteMaterial material;
    TDDraw::setupNative(&material, 1, 0, 2);
    material.setBlend(NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_INV_SRC_ALPHA);
    material.setDepth(true, NEBULA_COMPARE_ALWAYS, true);
    material.setTexture(0, texture);
    TypedDraw site = TDDraw::site(material, 2);
    VertexPT* vtx = site.vertices< VertexPT >(NEBULA_PRIMITIVE_QUADS, 4);
    *vtx++ = {minimum.x, minimum.y, 0.9999f, u0, v0};
    *vtx++ = {maximum.x, minimum.y, 0.9999f, u1, v0};
    *vtx++ = {maximum.x, maximum.y, 0.9999f, u1, v1};
    *vtx++ = {minimum.x, maximum.y, 0.9999f, u0, v1};
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
        mFpViewHidden = true;
        hideBeeFur();
    } else {
        mFpViewHidden = false;
        updateAlphaList(compareVal);
    }
}

void MarioActor::hideBeeFur() {
    if (mMario->isPlayerModeBee()) {
        mBeeWing->kill();
        mBeeFur->offDraw(-1);
    }

    if (getCarrySensor() != nullptr) {
        MR::hideModel(getCarrySensor()->mHost);
    }

    if (mMario->isPlayerModeInvincible()) {
        mBlurState = 0;
        stopEffect("無敵中");
    }
}

void MarioActor::calcFogLighting() {
    Color8 fog(240, 16, 80, 0);

    if (!mMario->isStatusActive(MarioStatus_Paralyze) && isNeedDamageFog()) {
        f32 phase = ((mFogWaveTimer + 31) & 63) / 63.0f;

        if (phase > 0.5f) {
            phase = 1.0f - phase;
        }

        f32 wave = 2.0f * phase;
        f32 low = getConst().getTable()->mDamageFogLow;
        mFogRate = (low + wave * (getConst().getTable()->mDamageFogHigh - getConst().getTable()->mDamageFogLow)) / 255.0f;
    } else if (mFlashFogTimer) {
        fog.mRGBA = mFlashFogColor.mRGBA;

        if (mFlashFogOn) {
            fog.g = (s32)(255.0f * MR::sin(FloatUtil::PI() * (mFlashFogTimer / 20.0f)));
        }

        mFogRate = mFlashFogRate * MR::sin((mFlashFogTimer / static_cast< f32 >(getConst().getTable()->mStarPieceFogTime)) * FloatUtil::PI());
    } else if (mMario->mItemDashTimer) {
        fog.set(255, 255, 0, 0);
        mFogRate = 0.8f * MR::sin(((mMario->mItemDashTimer / static_cast< f32 >(getConst().getTable()->mItemDashTimer)) * FloatUtil::PI()) / 2.0f);
    } else {
        resetFog();
    }

    Color8 ambient = *MR::getLightAmbientColor(this);
    updateLightList(ambient, Color8(255, 255, 255, 255), fog, mFogRate);
}

void MarioActor::resetFog() {
    Color8 color(0, 0, 0, 0);
    mFogWaveTimer = 0;
    mFogRate = 0.0f;
    mFlashFogTimer = 0;
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
        mLightTint.fogNear = MR::getRenderNearZ();
        mLightTint.fogFar = MR::getFarZ();
        mLightTint.fogColor = rFog.mRGBA;
        MR::calcFogStartEnd(mPosition, strength, &mLightTint.fogStart, &mLightTint.fogEnd);
    }
}

void MarioActor::createRainbowMesh() {
    for (u32 alpha = 0; alpha < cBlurPoseNum; alpha++) {
        for (u32 index = 0; index < cBlurPoseNum; index++) {
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
            mGhostTints[alpha * cBlurPoseNum + index].kind = MarioModelTint::Kind_Ghost;
            mGhostTints[alpha * cBlurPoseNum + index].color0 = color;
        }
    }
}

void MarioActor::drawScreenBlend() const {
    if (mMario->mCurrentState != nullptr && mMario->mCurrentState->getBlurOffset() != 0.0f) {
        MR::drawFullScreenBlur(mMario->mCurrentState->getBlurOffset());
    }

    if (mScreenBlurOn) {
        MR::drawFullScreenBlur(mScreenBlurGrow, mScreenBlurTrailGrow, mScreenBlurAlpha, mScreenBlurTrailAlpha);
    }

    if (mBeeWallWalk) {
        Vec2f position;
        Vec2f opposite;
        Vec2f minimum;
        Vec2f maximum;
        TDDraw::project2D(&position, mMario->mShadowPos);
        opposite = position + Vec2f(100.0f, 100.0f);
        position = position + Vec2f(-100.0f, -100.0f);

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

        position = minimum + Vec2f(-10.0f, -10.0f);
        opposite = maximum + Vec2f(10.0f, 10.0f);
        // Where the wall's shadow was marked: darkened, the mark going with it.
        SiteMaterial material;
        TDDraw::setupNative(&material, 0, 0, 2);
        material.setBlend(NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_INV_SRC_ALPHA);
        material.setStencil(NEBULA_STENCIL_IF_SET, NEBULA_STENCIL_ZERO);
        TypedDraw site = TDDraw::site(material, 2);
        TDDraw::drawFillBox(site, position, opposite, 0x00000040);
        MR::loadProjectionMtx();
        MR::loadViewMtx();
    }

    if (mDamageFlashTimer) {
        u32 color = 0xFF000080;

        if (mMario->mHitStates.swimLifeDrain) {
            color = 0x00004020;
        }

        SiteMaterial material;
        TDDraw::setupNative(&material, 0, 1, 2);
        TypedDraw site2 = TDDraw::site(material, 2);
        TDDraw::drawFillBox(site2, Vec3f(0.0f, 0.0f, 0.0f),
                            Vec3f(static_cast< f32 >(MR::getScreenWidth()), static_cast< f32 >(MR::getScreenHeight()), 0.0f), color);
        TDDraw::close();
    }
}

void MarioActor::updateRandomTexture(f32 distance) {
    mMaskIndex = 1 - mMaskIndex;
    u8* pixel;
    u8* image = mMaskTextures[mMaskIndex]->image();
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

    RenderTexture::linear(image, NEBULA_TEXTURE_LINEAR_LA8, 8, 8, 1);
}

namespace {
    inline Vec3f subtractOffset(const Vec3f& rBase, const Vec3f& rOffset) {
        const Vec3f* pOffset = &rOffset;
        return rBase - *pOffset;
    }
};  // namespace

void MarioActor::drawWallShade(const Vec3f& rPosition, const Vec3f& rNormal, f32 unused) const {
    f32 radius = 100.0f;
    SiteMaterial material;
    TDDraw::setupNative(&material, 1, 1, 0);
    material.setShader(NEBULA_SHADER_TEX_VERTEX_ALPHA);
    material.constants[0] = 0.0f;
    material.constants[1] = 1.0f;
    material.setDepth(true, NEBULA_COMPARE_GREATER, false);
    material.setTexture(0, mMaskTextures[mMaskIndex]);
    Mtx rotation;
    Vec3f tangent(0.0f, rNormal.z, -rNormal.y);

    if (MR::isNearZero(tangent)) {
        tangent.set< f32 >(rNormal.z, 0.0f, rNormal.x);
    }

    MR::normalizeOrZero(&tangent);
    NebulaMtxRotAxisRad(rotation, &rNormal, 0.3926991f);
    TypedDraw site = TDDraw::site(material, 0);
    VertexPCT* vtx = site.vertices< VertexPCT >(NEBULA_PRIMITIVE_TRIANGLE_FAN, 18);
    *vtx++ = {rPosition.x - 5.0f * rNormal.x, rPosition.y - 5.0f * rNormal.y, rPosition.z - 5.0f * rNormal.z, Rgba8(128), 0.0f, 0.0f};

    for (u32 i = 0; i <= 16; i++) {
        f32 angle = 2.0f * ((i / 16.0f) * FloatUtil::PI());
        Vec3f point = subtractOffset(rPosition, rNormal * 5.0f) + tangent * radius;
        *vtx++ = {point.x, point.y, point.z, Rgba8(1), 10.0f * Angle::cosRadian(angle), 10.0f * MR::sin(angle)};
        NebulaMtxMultVecSR(rotation, &tangent, &tangent);
    }
}

void MarioActor::drawSpinInhibit() const {
}

namespace {
    // runtime/src/render/materials/frame_ripple.hlsl over the whole screen: the frame's rows moved by the wave.
    void drawFrameRipple(const void* pFrame, f32 amplitude, f32 phase, f32 perRow, f32 kind) {
        SiteTexture texture;
        const u16 height = MR::getScreenHeight();
        SiteMaterial material;
        TDDraw::setupNative(&material, 1, 0, 2);
        material.init(NEBULA_SHADER_FRAME_RIPPLE, SiteMaterial::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        SiteTextures::init(&texture, const_cast< void* >(pFrame), NebulaFrameClock::cEffectTexelsAcross, NebulaFrameClock::cScreenHeight, NEBULA_WRAP_CLAMP,
                               NEBULA_WRAP_CLAMP);
        Frame::takeFrame(pFrame);
        material.mState.alpha_write = 1;
        material.setTexture(0, texture);
        material.constants[0] = amplitude;
        material.constants[1] = height;
        material.constants[2] = MR::getScreenWidth();
        material.constants[3] = kind;
        material.constants[4] = phase;
        material.constants[5] = perRow;
        TypedDraw site = TDDraw::site(material, 2);
    VertexPT* vtx = site.vertices< VertexPT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 4);
        *vtx++ = {0.0f, height, 0.0f, 0.0f, 1.0f};
        *vtx++ = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        *vtx++ = {MR::getScreenWidth(), height, 0.0f, 1.0f, 1.0f};
        *vtx++ = {MR::getScreenWidth(), 0.0f, 0.0f, 1.0f, 0.0f};
    }
}  // namespace

void MarioActor::drawColdWaterDamage() const {
    f32 frequency = 89.0f * MR::sin((TWO_PI * mMario->mSwim->mColdWaterDamageInterval) / 120.0f);

    if (frequency > 0.0f) {
        frequency = 0.0f;
    }

    drawFrameRipple(mRippleKey, 5.0f, mFrameCount, frequency, 0.0f);
}

void MarioActor::setRasterScroll(s32 amp, s32 phase, s32 wave) {
    mRasterScrollPhase = phase;
    mRasterScrollAmp = 0.9f * mRasterScrollAmp + 0.1f * amp;
    mRasterScrollWave = wave;
    mRasterScrollSet = 1;
}

void MarioActor::updateRasterScroll() {
    if (mRasterScrollSet) {
        mRasterScrollSet = 0;
        return;
    }

    mRasterScrollAmp *= 0.9f;

    if (mRasterScrollAmp < 1.0f) {
        mRasterScrollPhase = 0;
        mRasterScrollAmp = 0.0f;
    }

    if (mRasterScrollWave < 1.0f) {
        mRasterScrollWave = 0.0f;
    }
}

void MarioActor::drawRasterScroll(f32 amplitude, s16 period, f32 wavelength) const {
    if (wavelength == 0.0f) {
        return;
    }

    if (period == 0) {
        return;
    }

    const f32 phase = (mFrameCount * Angle::cRadian360) / period;
    drawFrameRipple(mRippleKey, amplitude, phase, TWO_PI / wavelength, 1.0f);
}

void MarioActor::drawMosaic() const {
}

void MarioActor::drawLifeUp() const {
}

void MarioActor::calcSpinEffect() {
    f32 baseRadius = 30.0f;
    f32 radiusScale = 5.8f;
    mSpinEffectRadius = 0.0f;
    mSpinEffectHeight = 0.0f;

    if (selectAction("スピン回復エフェクト") != 1) {
        return;
    }

    if (!mPunchFrame) {
        return;
    }

    if (mPunchHitTimer) {
        return;
    }

    if (!mSpinIntervalTimer) {
        return;
    }

    s32 frame = mSpinIntervalTimer - 9;

    if (frame <= 0 || frame > 15) {
        mSpinEffectRadius = 0.0f;
        return;
    }

    if (frame < 10) {
        mSpinEffectRadius = baseRadius + (frame / 10.0f) * (mPunchFrame * radiusScale);
        mSpinEffectHeight = 120.0f;
        return;
    }

    mSpinEffectRadius = baseRadius + mPunchFrame * radiusScale;
    mSpinEffectHeight = 120.0f;
}

void MarioActor::drawSpinEffect() const {
    if (mSpinEffectRadius == 0.0f) {
        return;
    }

    if (!isEnableNerveChange()) {
        return;
    }

    SiteMaterial material;
    TDDraw::setupNative(&material, 0, 2, 0);
    Mtx rotation;
    Vec3f center = mCenterPos + mMario->mHeadVec * mSpinEffectHeight;
    Vec3f direction(mMario->mFrontVec);
    Vec3f previous;
    NebulaMtxRotAxisRad(rotation, &mMario->mHeadVec, 0.09817477f);
    material.mState.line_width = 6.0f;
    TypedDraw site3 = TDDraw::site(material, 0);

    for (u32 i = 0; i <= 64; i++) {
        Vec3f point(direction);
        point.setLength(mSpinEffectRadius);
        point += mMario->mHeadVec * MR::getRandom() * 10.0f;

        if (i) {
            TDDraw::drawLine(site3, center + previous, center + point, 0xFFFFFF20);
        }

        previous = point;
        NebulaMtxMultVecSR(rotation, &direction, &direction);
    }

    material.mState.line_width = 3.0f;
    site3.setMaterial(material);

    for (u32 i = 0; i <= 64; i++) {
        Vec3f point(direction);
        point.setLength(mSpinEffectRadius);
        point += mMario->mHeadVec * MR::getRandom() * 10.0f;
        f32 wave = MR::sin((((mFrameCount + i) & 31) / 32.0f) * FloatUtil::PI());
        f32 square = wave * wave;
        u32 alpha = 160.0f * (square * square);

        if (i) {
            TDDraw::drawLine(site3, center + previous, center + point, alpha | 0x40FF00 | (alpha << 24));
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

    SiteMaterial material;
    TDDraw::setupNative(&material, 0, 2, 0);
    Frame::setZScaleOffset(1.0f, 0.00001f);
    material.mState.cull = NEBULA_CULL_FRONT;
    material.setDepth(true, NEBULA_COMPARE_GREATER, true);
    Vec3f position;
    getRealPos("Spine1", &position);
    TypedDraw site4 = TDDraw::site(material, 0);
    TDDraw::drawSphere3D(site4, position, 180.0f, 0xFFFFFF30, 16);
    Frame::setZScaleOffset(1.0f, 0.0f);
}

void MarioActor::initDarkMask() {
    for (u32 texture = 0; texture < 2; texture++) {
        u8* image = mMaskTextures[texture]->image();

        for (u32 i = 0; i < 8 * 8 * 2; i++) {
            image[i] = 0;
        }
        RenderTexture::linear(image, NEBULA_TEXTURE_LINEAR_LA8, 8, 8, 1);
    }

    mDarkMaskOn = true;
    mDarkMaskTimer = 0;
}

void MarioActor::updateDarkMask(u16 unused) {
    if (!mDarkMaskOn) {
        return;
    }

    u8* previous = mMaskTextures[mMaskIndex]->image();
    mMaskIndex = 1 - mMaskIndex;
    u8* image = mMaskTextures[mMaskIndex]->image();
    u8 x = MR::getRandom(0, 8);
    u8 y = MR::getRandom(0, 8);
    s32 index = 2 * (x + y * 8);
    image[index] = 0;
    image[index + 1] = 255;
    previous[index] = 0;
    previous[index + 1] = 255;
    RenderTexture::linear(image, NEBULA_TEXTURE_LINEAR_LA8, 8, 8, 1);
    mDarkMaskTimer++;
}

bool MarioActor::drawDarkMask() const {
    if (!mDarkMaskOn) {
        return false;
    }

    SiteMaterial material;
    TDDraw::setupNative(&material, 1, 0, 2);
    material.mState.color_write = 0;
    material.mState.alpha_write = 0;
    material.constants[0] = 1.0f;
    material.setTexture(0, mMaskTextures[mMaskIndex]);
    material.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ADD);
    Vec3f spine;
    Vec2f center;
    Vec2f screenX;
    Vec2f screenY;
    Vec2f origin;
    Vec2f minimum;
    Vec2f maximum;
    getRealPos("Spine1", &spine);
    f32 radius = mDarkMaskTimer;

    if (radius >= 240.0f) {
        radius = 240.0f;
    }

    Vec3f position(mPosition);
    TDDraw::project2D(&center, position);
    TDDraw::project2D(&screenX, position + mCamDirX * radius);
    TDDraw::project2D(&screenY, position + mCamDirY * radius);
    u16 width = ((static_cast< u32 >((screenX - center).length()) + 3) & ~3) + 4;
    u16 height = ((static_cast< u32 >((screenY - center).length()) + 3) & ~3) + 4;
    TDDraw::project2D(&origin, position);
    origin.x -= width / 2;
    origin.y -= width / 2;
    minimum = origin;
    maximum = origin + Vec2f(width, height);
    TypedDraw site = TDDraw::site(material, 2);
    VertexPT* vtx = site.vertices< VertexPT >(NEBULA_PRIMITIVE_QUADS, 4);
    *vtx++ = {minimum.x, minimum.y, 0.0f, 0.0f, 0.0f};
    *vtx++ = {maximum.x, minimum.y, 0.0f, 4.0f, 0.0f};
    *vtx++ = {maximum.x, maximum.y, 0.0f, 4.0f, 4.0f};
    *vtx++ = {minimum.x, maximum.y, 0.0f, 0.0f, 4.0f};
    TDDraw::close();
    return true;
}

void MarioActor::showBeeFur() {
    if (mMario->isPlayerModeBee()) {
        mBeeWing->appear();
        mBeeFur->onDraw(-1);
    }

    if (getCarrySensor() != nullptr) {
        MR::showModel(getCarrySensor()->mHost);
    }

    if (mMario->isPlayerModeInvincible()) {
        MR::showJoint(getModel(), "Face0");
        mBlurState = 2;
        playEffect("無敵中");
    }
}

f32 MarioState::getBlurOffset() const {
    return 0.0f;
}
