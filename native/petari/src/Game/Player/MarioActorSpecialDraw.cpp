#include "Game/LiveActor/HitSensor.hpp"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/Player/DLchanger.hpp"
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
#include "Game/Util/SchedulerUtil.hpp"
#include <JSystem/JGeometry/TBox.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <new>
#include <revolution/gx/GXEnum.h>
#include <revolution/gx/GXFrameBuf.h>
#include <revolution/gx/GXPixel.h>
#include <revolution/os.h>

void MarioActorSpecialDraw_FORCE_MATCH_SDATA2() {
    (void)80.0f;
    (void)0.99000001f;
    (void)0.999899983f;
    (void)300.0f;
    (void)1000.0f;
    (void)255.0f;
    (void)63.0f;
    (void)20.0f;
    (void)0.800000012f;
    (void)2607.59448f;
}

void MarioActor::initScreenBox() {
    _B44 = new (32) u8[0x20000];
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
    TVec2f frameMin;
    TVec2f frameMax;
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
    MR::convertScreenPosToFrameBufferPos(&frameMin, mScreenBoxMin);
    MR::convertScreenPosToFrameBufferPos(&frameMax, mScreenBoxMax);
    s16 left = static_cast< s32 >(frameMin.x) & ~1;
    s16 top = static_cast< s32 >(frameMin.y) & ~1;
    s16 width = static_cast< s32 >(frameMax.x) - left;
    s16 height = static_cast< s32 >(frameMax.y) - top;
    width = (width + 1) & ~1;
    height = (height + 1) & ~1;

    if (width > 256) {
        width = 256;
    }

    if (height > 256) {
        height = 256;
    }

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
    TBox2f screen(0.0f, 0.0f, MR::getFrameBufferWidth(), screenHeight);
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

    J3DNativeState::setTexCopyDst((s32)(_B3C), (s32)(_B40), GX_TF_RGB565, GX_FALSE);
    J3DNativeState::setTexCopySrc((s32)(_B34), (s32)(_B38), (s32)(_B3C), (s32)(_B40));
    J3DNativeState::setColorUpdate(GX_FALSE);
    J3DNativeState::setAlphaUpdate(GX_TRUE);

    GXColor clear = {};

    J3DNativeState::setCopyClear(clear, 0xFFFFFF);
    J3DNativeState::copyTex(_B44, GX_TRUE);
    J3DNativeState::setColorUpdate(GX_ENABLE);
    J3DNativeState::setAlphaUpdate(GX_FALSE);
    J3DNativeState::setDstAlpha(GX_FALSE, 0);
}

void MarioActor::writeBackScreenBox() const {
    if (!isUseScreenBox()) {
        return;
    }

    if (_B3C < 1.0f || _B40 < 1.0f) {
        return;
    }

    GXTexObj texture;
    TVec2f minimum;
    TVec2f maximum;
    TDDraw::setup(1, 0, 2);
    J3DNativeTexture::init(&texture, _B44, (s32)(_B3C), (s32)(_B40), GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
    J3DNativeTexture::load(&texture, GX_TEXMAP0);
    MR::convertFrameBufferPosToScreenPos(&minimum, mScreenBoxPos);
    MR::convertFrameBufferPosToScreenPos(&maximum, mScreenBoxPos + mScreenBoxSize + TVec2f(0.99f, 0.99f));
    J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_INVDSTALPHA, GX_BL_DSTALPHA, GX_LO_NOOP);
    J3DNativeState::setDstAlpha(GX_TRUE, 0);
    J3DNativeState::setAlphaUpdate(GX_TRUE);
    maximum.x = static_cast< s16 >(static_cast< s16 >(static_cast< s32 >(maximum.x) + 1) & ~1);
    J3DNativeState::setZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
    J3DNativeDraw::begin(GX_QUADS, 4, NEBULA_SHAPE_TEXCOORD0);
    J3DNativeDraw::position(minimum.x, minimum.y, 0.9999f);
    J3DNativeDraw::texCoord(0.0f, 0.0f);
    J3DNativeDraw::position(maximum.x, minimum.y, 0.9999f);
    J3DNativeDraw::texCoord(1.0f, 0.0f);
    J3DNativeDraw::position(maximum.x, maximum.y, 0.9999f);
    J3DNativeDraw::texCoord(1.0f, 1.0f);
    J3DNativeDraw::position(minimum.x, maximum.y, 0.9999f);
    J3DNativeDraw::texCoord(0.0f, 1.0f);
    J3DNativeState::setDstAlpha(GX_FALSE, 0);
    J3DNativeState::setAlphaUpdate(GX_FALSE);
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
        updateAlphaDL(compareVal);
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
        fog.mGXColor = _1B0.mGXColor;

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
    updateLightDL(ambient, Color8(255, 255, 255, 255), fog, _1A4);
}

void MarioActor::resetFog() {
    Color8 color(0, 0, 0, 0);
    _1A8 = 0;
    _1A4 = 0.0f;
    _1AA = 0;
}

void MarioActor::updateAlphaDL(u8 alpha) {
    NebulaStateOp storage[16];
    J3DStateList list(storage, 16);
    GXColor color = {0, 0, 0, 255};
    color.a = alpha;
    list.setTevColor(GX_TEVREG2, color);
    list.setBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
    list.setGenMode2(1, 0, 1, 0, GX_CULL_BACK);
    list.setChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_CLAMP, GX_AF_NONE);
    list.setChanCtrl(GX_COLOR1A1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_CLAMP, GX_AF_NONE);
    list.setAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    list.setTevAlphaCalcAndSwap(GX_TEVSTAGE0, GX_CA_A2, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV,
                             GX_TEV_SWAP0, GX_TEV_SWAP0);
    list.setTevColorCalc(GX_TEVSTAGE0, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    list.setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL, GX_TEXCOORD1, GX_TEXMAP1, GX_COLOR_NULL);
    mDLchanger->swap()->mList.copy(list);
}

void MarioActor::updateSimpleAlphaDL(u8 alpha) {
    NebulaStateOp storage[16];
    J3DStateList list(storage, 16);
    list.setDstAlpha(GX_TRUE, alpha);
    mDLchanger->swap()->mList.copy(list);
}

void MarioActor::updateReflectAlphaDL(u8 alpha) {
    NebulaStateOp storage[16];
    J3DStateList list(storage, 16);
    GXColor color = {0, 0, 0, 255};
    color.a = alpha;
    list.setTevColor(GX_TEVREG2, color);
    list.setTevAlphaCalcAndSwap(GX_TEVSTAGE0, GX_CA_A2, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV,
                             GX_TEV_SWAP0, GX_TEV_SWAP0);
    mDLchanger->swap()->mList.copy(list);
}

void MarioActor::updateLightDL(const Color8& rAmbient, const Color8& rMaterial, const Color8& rFog, f32 strength) {
    NebulaStateOp storage[16];
    J3DStateList list(storage, 16);
    GXColor ambientColor = rAmbient.mGXColor;
    list.setChanAmbColor(GX_COLOR0A0, ambientColor);
    GXColor materialColor = rMaterial.mGXColor;
    list.setChanMatColor(GX_COLOR0A0, materialColor);
    list.setChanCtrl(GX_COLOR1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_CLAMP, GX_AF_NONE);

    if (!MR::isNearZero(strength)) {
        f32 nearZ = MR::getNearZ();
        f32 farZ = MR::getFarZ();
        GXColor color = rFog.mGXColor;
        f32 start;
        f32 end;
        MR::calcFogStartEnd(mPosition, strength, &start, &end);
        list.setFog(GX_FOG_LIN, start, end, nearZ, farZ, color);
    }

    mCurrDL = 1 - mCurrDL;
    mDL[mCurrDL].copy(list);
}

void MarioActor::createRainbowDL() {
    NebulaStateOp storage[1];
    J3DStateList list(storage, 1);

    for (u32 alpha = 0; alpha < 8; alpha++) {
        for (u32 index = 0; index < 8; index++) {
            _94[alpha * 8 + index] = new DLchanger(1, 1);
            GXColor color = {0, 0, 0, 0};

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
            list.setTevColor(GX_TEVREG0, color);
            DLchanger* changer = _94[alpha * 8 + index];
            changer->mBuffers[changer->mCurrentBuffer].mList.copy(list);
            list.clear();
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
        TDDraw::setup(0, 0, 2);
        J3DNativeState::setCullMode(GX_CULL_NONE);
        J3DNativeState::setAlphaUpdate(GX_FALSE);
        J3DNativeState::setColorUpdate(GX_TRUE);
        J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_INVDSTALPHA, GX_LO_NOOP);
        J3DNativeState::setDstAlpha(GX_TRUE, 0);
        TDDraw::drawFillBox(position, opposite, 0);
        J3DNativeState::setAlphaUpdate(GX_FALSE);
        MR::loadProjectionMtx();
        MR::loadViewMtx();
    }

    if (_BC4) {
        u32 color = 0xFF000080;

        if (mMario->_1C._3) {
            color = 0x00004020;
        }

        TDDraw::setup(0, 1, 2);
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

    for (u32 y = 0; y < 8; y++) {
        pixel = image + y * 8;

        for (u32 x = 0; x < 8; x++) {
            s32 alpha = *pixel >> 4;

            if (MR::getRandom() < probability) {
                alpha += 4;
            } else {
                alpha--;
            }

            s32 clamped = alpha < 0 ? 0 : alpha > 15 ? 15 : alpha;
            *pixel = (clamped * 16) & 0xF0;
            pixel++;
        }
    }

    J3DNativeTexture::registerImage(image, GX_TF_IA4, 8, 8, 1);
}

namespace {
    inline TVec3f subtractOffset(const TVec3f& rBase, const TVec3f& rOffset) {
        const TVec3f* pOffset = &rOffset;
        return rBase - *pOffset;
    }
};  // namespace

#pragma push
#pragma global_optimizer off
void MarioActor::drawWallShade(const TVec3f& rPosition, const TVec3f& rNormal, f32 unused) const {
    f32 radius = 100.0f;
    TDDraw::setup(1, 1, 0);
    J3DNativeState::setZMode(GX_TRUE, GX_GREATER, GX_FALSE);
    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    mMaskTextures[_B88]->load(GX_TEXMAP0);
    Mtx rotation;
    TVec3f tangent(0.0f, rNormal.z, -rNormal.y);

    if (MR::isNearZero(tangent)) {
        tangent.set< f32 >(rNormal.z, 0.0f, rNormal.x);
    }

    MR::normalizeOrZero(&tangent);
    PSMTXRotAxisRad(rotation, &rNormal, 0.3926991f);
    J3DNativeDraw::begin(GX_TRIANGLEFAN, 18, NEBULA_SHAPE_COLOR0 | NEBULA_SHAPE_TEXCOORD0);
    J3DNativeDraw::position(rPosition.x - 5.0f * rNormal.x, rPosition.y - 5.0f * rNormal.y, rPosition.z - 5.0f * rNormal.z);
    J3DNativeDraw::color(128);
    J3DNativeDraw::texCoord(0.0f, 0.0f);

    for (u32 i = 0; i <= 16; i++) {
        f32 angle = 2.0f * ((i / 16.0f) * JGeometry::TUtil< f32 >::PI());
        TVec3f point = subtractOffset(rPosition, rNormal * 5.0f) + tangent * radius;
        J3DNativeDraw::position(point.x, point.y, point.z);
        J3DNativeDraw::color(1);
        J3DNativeDraw::texCoord(10.0f * JMACosRadian(angle), 10.0f * MR::sin(angle));
        PSMTXMultVecSR(rotation, &tangent, &tangent);
    }
}
#pragma pop

void MarioActor::drawSpinInhibit() const {
}

void MarioActor::drawColdWaterDamage() const {
    u32 next;
    u32 stripHeight = 4;
    GXTexObj textures[2];
    u16 width = MR::getFrameBufferWidth();
    TDDraw::setup(1, 0, 2);
    u16 current = 0;
    J3DNativeTexture::init(&textures[0], _1D8, width, 4, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
    J3DNativeTexture::init(&textures[1], _1DC, width, 4, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
    J3DNativeState::setLineWidth(6, GX_TO_ZERO);
    J3DNativeState::setTexCopyDst(width, 4, GX_TF_RGB565, GX_FALSE);
    J3DNativeState::setTexCopySrc(0, 0, width, 4);
    J3DNativeState::copyTex(_1D8, GX_FALSE);
    f32 textureStep = 1.0f / static_cast< f32 >(stripHeight);
    f32 frequency = 89.0f * MR::sin((TWO_PI * mMario->mSwim->mColdWaterDamageInterval) / 120.0f);

    if (frequency > 0.0f) {
        frequency = 0.0f;
    }

    for (u32 y = 0; y < MR::getScreenHeight(); y += 4) {
        J3DNativeState::setTexCopySrc(0, y + 4, width, 4);
        next = 1 - current;
        J3DNativeState::copyTex(mRasterBuffers[next], GX_FALSE);
        J3DNativeTexture::load(&textures[current], GX_TEXMAP0);
        f32 v = 0.0f;

        for (u32 row = y; row < y + 4; row++) {
            f32 offset = 5.0f * MR::sin(2.0f * (((_37C + row * frequency) / MR::getScreenHeight()) * JGeometry::TUtil< f32 >::PI()));
            J3DNativeDraw::begin(GX_LINES, 2, NEBULA_SHAPE_TEXCOORD0);
            J3DNativeDraw::position(offset, row, 0.0f);
            J3DNativeDraw::texCoord(0.0f, v);
            J3DNativeDraw::position(offset + MR::getScreenWidth(), row, 0.0f);
            J3DNativeDraw::texCoord(1.0f, v);
            v += textureStep;
        }

        current = next;
    }
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

    u32 next;
    u32 stripHeight = 4;
    GXTexObj textures[2];
    u16 width = MR::getFrameBufferWidth();
    TDDraw::setup(1, 0, 2);
    u16 current = 0;
    J3DNativeTexture::init(&textures[0], _1D8, width, 4, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
    J3DNativeTexture::init(&textures[1], _1DC, width, 4, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
    J3DNativeState::setLineWidth(6, GX_TO_ZERO);
    J3DNativeState::setTexCopyDst(width, 4, GX_TF_RGB565, GX_FALSE);
    J3DNativeState::setTexCopySrc(0, 0, width, 4);
    J3DNativeState::copyTex(_1D8, GX_FALSE);
    f32 textureStep = 1.0f / static_cast< f32 >(stripHeight);
    f32 phase = (_37C * JMath::TAngleConstant_< f32 >::RADIAN_DEG360()) / period;
    f32 spatialPhase = TWO_PI / wavelength;

    for (u32 y = 0; y < MR::getScreenHeight(); y += 4) {
        J3DNativeState::setTexCopySrc(0, y + 4, width, 4);
        next = 1 - current;
        J3DNativeState::copyTex(mRasterBuffers[next], GX_FALSE);
        J3DNativeTexture::load(&textures[current], GX_TEXMAP0);
        f32 v = 0.0f;

        for (u32 row = y; row < y + 4; row++) {
            f32 angle = MR::sin(spatialPhase * row) * JGeometry::TUtil< f32 >::PI();
            f32 offset = amplitude * MR::sin(angle + phase);
            J3DNativeDraw::begin(GX_LINES, 2, NEBULA_SHAPE_TEXCOORD0);
            J3DNativeDraw::position(offset, row, 0.0f);
            J3DNativeDraw::texCoord(0.0f, v);
            J3DNativeDraw::position(offset + MR::getScreenWidth(), row, 0.0f);
            J3DNativeDraw::texCoord(1.0f, v);
            v += textureStep;
        }

        current = next;
    }
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

    TDDraw::setup(0, 1, 0);
    J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_NOOP);
    Mtx rotation;
    TVec3f center = _2A0 + mMario->mHeadVec * _6D8;
    TVec3f direction(mMario->mFrontVec);
    TVec3f previous;
    PSMTXRotAxisRad(rotation, &mMario->mHeadVec, 0.09817477f);
    J3DNativeState::setLineWidth(36, GX_TO_ZERO);

    for (u32 i = 0; i <= 64; i++) {
        TVec3f point(direction);
        point.setLength(_6D4);
        point += mMario->mHeadVec * MR::getRandom() * 10.0f;

        if (i) {
            TDDraw::drawLine(center + previous, center + point, 0xFFFFFF20);
        }

        previous = point;
        PSMTXMultVecSR(rotation, &direction, &direction);
    }

    J3DNativeState::setLineWidth(18, GX_TO_ZERO);

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
        PSMTXMultVecSR(rotation, &direction, &direction);
    }

    TDDraw::close();
}

void MarioActor::drawSphereMask() const {
    if (!mMario->isVisibleRecoveryWarpBubble()) {
        return;
    }

    TDDraw::setup(0, 1, 0);
    J3DNativeView::setZScaleOffset(1.0f, 0.00001f);
    J3DNativeState::setCullMode(GX_CULL_FRONT);
    J3DNativeState::setZMode(GX_TRUE, GX_GREATER, GX_TRUE);
    J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_NOOP);
    TVec3f position;
    getRealPos("Spine1", &position);
    TDDraw::drawSphere3D(position, 180.0f, 0xFFFFFF30, 16);
    J3DNativeView::setZScaleOffset(1.0f, 0.0f);
}

void MarioActor::initDarkMask() {
    for (u32 texture = 0; texture < 2; texture++) {
        u8* image = mMaskTextures[texture]->mImage;

        for (u32 y = 0; y < 8; y++) {
            u8* pixel = image + y * 8;

            for (u32 x = 0; x < 8; x++) {
                pixel[x] = 0;
            }
        }
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
    u8 x = MR::getRandom(0L, 8L);
    u8 y = MR::getRandom(0L, 8L);
    s32 index = x + y * 8;
    image[index] = 0xF0;
    previous[index] = 0xF0;
    J3DNativeTexture::registerImage(image, GX_TF_IA4, 8, 8, 1);
    _1C4++;
}

bool MarioActor::drawDarkMask() const {
    if (!_1C3) {
        return false;
    }

    TDDraw::setup(1, 0, 2);
    J3DNativeState::setAlphaUpdate(GX_TRUE);
    J3DNativeState::setColorUpdate(GX_FALSE);
    J3DNativeState::setZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    mMaskTextures[_B88]->load(GX_TEXMAP0);
    J3DNativeState::setZCompLoc(GX_FALSE);
    J3DNativeState::setAlphaCompare(GX_GREATER, 1, GX_AOP_AND, GX_ALWAYS, 0);
    J3DNativeState::setDstAlpha(GX_TRUE, 255);
    TVec3f spine;
    TVec2f center;
    TVec2f screenX;
    TVec2f screenY;
    TVec2f origin;
    TVec2f frameOrigin;
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
    MR::convertScreenPosToFrameBufferPos(&frameOrigin, origin);
    u32 left = static_cast< u32 >(frameOrigin.x);
    u32 top = static_cast< u32 >(frameOrigin.y);
    top &= ~1;
    left &= ~1;
    MR::convertFrameBufferPosToScreenPos(&minimum, TVec2f(left, top));
    MR::convertFrameBufferPosToScreenPos(&maximum, TVec2f(left + width, top + height));
    J3DNativeDraw::begin(GX_QUADS, 4, NEBULA_SHAPE_TEXCOORD0);
    J3DNativeDraw::position(minimum.x, minimum.y, 0.0f);
    J3DNativeDraw::texCoord(0.0f, 0.0f);
    J3DNativeDraw::position(maximum.x, minimum.y, 0.0f);
    J3DNativeDraw::texCoord(4.0f, 0.0f);
    J3DNativeDraw::position(maximum.x, maximum.y, 0.0f);
    J3DNativeDraw::texCoord(4.0f, 4.0f);
    J3DNativeDraw::position(minimum.x, maximum.y, 0.0f);
    J3DNativeDraw::texCoord(0.0f, 4.0f);
    J3DNativeState::setDstAlpha(GX_FALSE, 0);
    J3DNativeState::setColorUpdate(GX_TRUE);
    J3DNativeState::setZCompLoc(GX_TRUE);
    J3DNativeState::setAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
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
