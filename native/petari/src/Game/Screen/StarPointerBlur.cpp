#include "Game/Screen/StarPointerBlur.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/System/NebulaInput.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Screen/StarPointerDirector.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Render/GameTexture.hpp"
#include <NebulaGpuTypes.h>
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sBeginThick = 10.0f;
    static const s32 BLUR_POINT_NUM = 20;
    const f32 sEndThick = 0.0f;
};  // namespace

StarPointerBlur::~StarPointerBlur() {
}

StarPointerBlur::StarPointerBlur(GameTexture* pTexture)
    : NameObj("スターポインタ用ブラー"), mTexture(pTexture), mPadChannel(PLAYER_1), mMainColor(nullptr), mBlurPoints(nullptr),
      mBlurThicks(nullptr), mBlurTexCoords(nullptr), mNumHiddenPoints(0) {
    lumacore::setClass(this, lumacore::kClassId< StarPointerBlur >);
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
        Vec2f pastPos = StarPointerFunction::getNextPastPosition(mPadChannel, idx);
        StarPointerFunction::forceInsideScreenEdge(&pastPos);
        updatePoint(pastPos, false, nullptr);
    }
}

void StarPointerBlur::updatePoint(const Vec2f& rPos, bool hide, Vec2f* pDir) {
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

    const SiteMaterial material = loadMaterial();
    TypedDraw site = TypedDraw::eyeSpace(material, cIdentityMtx);
    draw2(site);
}

void StarPointerBlur::draw2(TypedDraw& rDraw) const {
    u16 start, end;

    Vec2f dir(0.0f, 1.0f);
    start = 0;
    end = 0;
    while (start < BLUR_POINT_NUM) {
        u16 range = findDrawPointRange(&start, &end);
        if (range < 2) {
            start = end;
            continue;
        }

        VertexPT* vtx = rDraw.vertices< VertexPT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, range * 2);
        for (s32 point = start; point < end; point++) {
            const Vec2f& pos = mBlurPoints[point].mPos;
            const Vec2f& offset = dir * mBlurThicks[point];
            *vtx++ = {pos.x - offset.x, pos.y - offset.y, 0.0f, 0.0f, mBlurTexCoords[point]};
            *vtx++ = {pos.x + offset.x, pos.y + offset.y, 0.0f, 1.0f, mBlurTexCoords[point]};
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

        VertexPT* vtx = rDraw.vertices< VertexPT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, range * 2);
        for (s32 point = start; point < end; point++) {
            const Vec2f& pos = mBlurPoints[point].mPos;
            const Vec2f& offset = dir * mBlurThicks[point];
            *vtx++ = {pos.x - offset.x, pos.y - offset.y, 0.0f, 0.0f, mBlurTexCoords[point]};
            *vtx++ = {pos.x + offset.x, pos.y + offset.y, 0.0f, 1.0f, mBlurTexCoords[point]};
        }

        start = end;
    }
}

SiteMaterial StarPointerBlur::loadMaterial() const {
    // runtime/src/render/materials/color_tex_alpha.hlsl: mMainColor at the trail texture's alpha, added by it; depth
    // tested and written, no culling.
    SiteMaterial material;
    material.init(NEBULA_SHADER_COLOR_TEX_ALPHA, SiteMaterial::BLEND_ADD_ALPHA, NEBULA_COMPARE_LEQUAL, true,
                       NEBULA_CULL_NONE);
    material.setColor(0, mMainColor->r, mMainColor->g, mMainColor->b, mMainColor->a);
    material.constants[4] = -1.0f;
    material.setTexture(0, mTexture);
    return material;
}
