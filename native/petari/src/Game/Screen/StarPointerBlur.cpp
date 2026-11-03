#include "Game/Screen/StarPointerBlur.hpp"
#include "Game/Util/NativeShader.hpp"
#include "Game/System/NebulaInput.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/Screen/StarPointerDirector.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include <NebulaGpuTypes.h>

namespace {
    static const f32 sBeginThick = 10.0f;
    static const s32 BLUR_POINT_NUM = 20;
    static const s32 sDrawMode = 2;
    static const f32 sEndThick = 0.0f;
};  // namespace

StarPointerBlur::~StarPointerBlur() {
}

StarPointerBlur::StarPointerBlur(JUTTexture* pTexture)
    : NameObj("\203\130\203\136\201\133\203\174\203\103\203\223\203\136\227\160\203\165\203\211\201\133" /* スターポインタ用ブラー */), mTexture(pTexture), mPadChannel(PLAYER_1), mMainColor(nullptr), mSubColor(nullptr), mBlurPoints(nullptr),
      mBlurThicks(nullptr), mBlurTexCoords(nullptr), mNumHiddenPoints(0), _2C(false), _2D(false), _30(0), _34(0), _38(0), _3C(0) {
    mBlurPoints = new BlurPoint[BLUR_POINT_NUM];
    mBlurThicks = new f32[BLUR_POINT_NUM];
    mBlurTexCoords = new f32[BLUR_POINT_NUM];

    f32 interval = 1.0f / BLUR_POINT_NUM;
    for (s32 idx = 0; idx < BLUR_POINT_NUM; idx++) {
        f32 t = idx * interval;
        mBlurThicks[idx] = (1.0f - t) * ::sBeginThick + t * ::sEndThick;
        mBlurTexCoords[idx] = t;
    }
}

StarPointerBlur::BlurPoint::BlurPoint() : mPos(0.0f, 0.0f), mDir(0.0f, 1.0f), mIsHidden(true) {
}

void StarPointerBlur::show() {
    updatePoint(MR::getStarPointerScreenPositionOrEdge(mPadChannel), false, nullptr);
}

void StarPointerBlur::hide() {
    if (mNumHiddenPoints >= BLUR_POINT_NUM) {
        return;
    }

    updatePoint(mBlurPoints[0].mPos, true, &mBlurPoints[0].mDir);
}

void StarPointerBlur::hideAll() {
    if (mNumHiddenPoints >= BLUR_POINT_NUM) {
        return;
    }

    for (s32 idx = 0; idx < BLUR_POINT_NUM; idx++) {
        mBlurPoints[idx].mIsHidden = true;
    }

    mNumHiddenPoints = BLUR_POINT_NUM;
}

void StarPointerBlur::movement() {
    // This frame's positions, as the cursor (StarPointerLayout::setPosition): its head stays on the cursor.
    s32 pastPointNum = StarPointerFunction::getNextPastPointNum(mPadChannel);

    if (pastPointNum <= 0) {
        hide();
        return;
    }

    for (s32 idx = pastPointNum - 1; idx >= 0; idx--) {
        TVec2f pastPos = StarPointerFunction::getNextPastPosition(mPadChannel, idx);
        StarPointerFunction::forceInsideScreenEdge(&pastPos);
        updatePoint(pastPos, false, nullptr);
    }
}

void StarPointerBlur::updatePoint(const TVec2f& rPos, bool hide, TVec2f* pDir) {
    if (hide == true) {
        mNumHiddenPoints++;
    } else {
        mNumHiddenPoints = 0;
    }

    for (s32 idx = BLUR_POINT_NUM - 1; idx > 0; idx--) {
        mBlurPoints[idx] = mBlurPoints[idx - 1];
    }

    mBlurPoints[0].mPos.set(rPos);
    mBlurPoints[0].mIsHidden = hide;

    if (pDir != nullptr) {
        mBlurPoints[0].mDir.set(*pDir);
    } else {
        mBlurPoints[0].mDir = rPos - mBlurPoints[1].mPos;
    }

    MR::normalizeOrZero(&mBlurPoints[0].mDir);
}

u16 StarPointerBlur::findDrawPointRange(u16* start, u16* end) const {

    u16 range;
    u16 idx = *start;
    for (; idx < BLUR_POINT_NUM; idx++) {
        if (mBlurPoints[idx].mIsHidden == false) {
            *start = idx;
            range = 1;
            goto findEnd;
        }
    }

    *end = BLUR_POINT_NUM;
    *start = BLUR_POINT_NUM;
    return 0;

findEnd:
    for (idx = *start + 1; idx < BLUR_POINT_NUM; idx++) {
        if (mBlurPoints[idx].mIsHidden == true) {
            *end = idx;
            return range;
        }
        range++;
    }

    *end = BLUR_POINT_NUM;
    return range;
}

void StarPointerBlur::draw() const {
    if (mNumHiddenPoints >= BLUR_POINT_NUM) {
        return;
    }

    initDraw();
    switch (::sDrawMode) {
    case 0:
        draw0((NebulaColor8){0, 0, 0, 0});  // NOTE: indicated by debug symbols, color is a dummy
        break;
    case 1:
        draw1();  // NOTE: indicated by debug symbols
        break;
    case 2:
        draw2();
        break;
    }
}

void StarPointerBlur::draw1() const {
}

void StarPointerBlur::draw2() const {
    u16 start, end;

    TVec2f dir(0.0f, 1.0f);
    start = 0;
    end = 0;
    while (start < BLUR_POINT_NUM) {
        u16 range = findDrawPointRange(&start, &end);
        if (range < 2) {
            start = end;
            continue;
        }

        J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLE_STRIP, range * 2, NEBULA_SHAPE_TEXCOORD0);
        for (s32 point = start; point < end; point++) {
            const TVec2f& pos = mBlurPoints[point].mPos;
            const TVec2f& offset = dir * mBlurThicks[point];
            J3DNativeDraw::position(pos.x - offset.x, pos.y - offset.y, 0.0f);
            J3DNativeDraw::texCoord(0.0f, mBlurTexCoords[point]);
            J3DNativeDraw::position(pos.x + offset.x, pos.y + offset.y, 0.0f);
            J3DNativeDraw::texCoord(1.0f, mBlurTexCoords[point]);
        }

        start = end;
    }

    dir.set(1.0f, 0.0f);
    start = 0;
    end = 0;
    while (start < BLUR_POINT_NUM) {
        u16 range = findDrawPointRange(&start, &end);
        if (range < 2) {
            start = end;
            continue;
        }

        J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLE_STRIP, range * 2, NEBULA_SHAPE_TEXCOORD0);
        for (s32 point = start; point < end; point++) {
            const TVec2f& pos = mBlurPoints[point].mPos;
            const TVec2f& offset = dir * mBlurThicks[point];
            J3DNativeDraw::position(pos.x - offset.x, pos.y - offset.y, 0.0f);
            J3DNativeDraw::texCoord(0.0f, mBlurTexCoords[point]);
            J3DNativeDraw::position(pos.x + offset.x, pos.y + offset.y, 0.0f);
            J3DNativeDraw::texCoord(1.0f, mBlurTexCoords[point]);
        }

        start = end;
    }
}

void StarPointerBlur::initDraw() const {
    TPos3f mtx;
    mtx.identity();
    J3DNativeShape::setPosition(0, mtx);
    J3DNativeShape::setCurrentIdentity();
    // runtime/src/render/materials/color_tex_alpha.hlsl: mMainColor at the trail texture's alpha, added by it; depth
    // tested and written, no culling.
    NebulaNativeMaterial material;
    NativeShader::init(material, NEBULA_SHADER_COLOR_TEX_ALPHA, NativeShader::BLEND_ADD_ALPHA, NEBULA_COMPARE_LEQUAL, true,
                       NEBULA_CULL_NONE);
    NativeShader::setColor(material, 0, mMainColor->r, mMainColor->g, mMainColor->b, mMainColor->a);
    material.constants[4] = -1.0f;
    NativeShader::setTexture(material, 0, mTexture);
    NativeShader::send(material);
}
