#include "Game/Util/CameraUtil.hpp"
#include "Game/MapObj/SnowFloorTile.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/RenderTexture.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/Util/Array.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const u16 sTextureSize = 32;
    const u16 sStripCount = 16;
    const s16 sLayerCount = 12;
    const s16 sColorR = 220;
    const s16 sColorStepR = -3;
    const s16 sColorG = 220;
    const s16 sColorStepG = -3;
    const s16 sColorB = 250;
    const s16 sColorStepB = -3;
    const s16 sColorA = 64;
    const s16 sColorStepA = 8;
    const s16 sLayerHeight = 6;
    const f32 cPointerDigAmount = 30.0f;
    const f32 cPlayerDigAmount = 600.0f;
    const f32 cFireballDigAmount = 200.0f;
    const f32 cSwingDigAmount = 20.0f;
    const f32 cSwingDigRadius = 100.0f;
    const f32 cHipdropDigAmount = 1000.0f;
    const f32 cHipdropDigRadius = 50.0f;
};  // namespace

SnowFloorTile::SnowFloorTile(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< SnowFloorTile >);
}

void SnowFloorTile::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    // A tile with no scale takes scale 1; its sheet holds 1 to 4096 cells a side (a scale of 128; a mod's absurd scale
    // asked for billions).
    const f32 tileScale = mScale.x > 0.0f ? mScale.x : 1.0f;
    f32 sizeValue = ::sTextureSize * tileScale;
    if (sizeValue > 4096.0f) {
        sizeValue = 4096.0f;
    } else if (sizeValue < 1.0f) {
        sizeValue = 1.0f;
    }

    mTextureSize = static_cast< s32 >(sizeValue);
    mTextureShift = 0;
    u32 size = mTextureSize;

    for (u32 i = 0; i < mTextureSize; i++) {
        if ((size >>= 1) == 0) {
            break;
        }

        mTextureShift++;
    }

    mCellSize = 128.0f * tileScale / ::sTextureSize;
    initHitSensor(1);
    MR::addHitSensorEye(this, "eye", 16, 1.4142f * (mTextureSize * mCellSize), Vec3f(0.0f, 0.0f, 0.0f));
    mSnowDepth = new f32[mTextureSize * mTextureSize];

    for (u32 y = 0; y < mTextureSize; y++) {
        for (u32 x = 0; x < mTextureSize; x++) {
            mSnowDepth[(y << mTextureShift) + x] = 32.0f + 120.0f * MR::getRandom();
        }
    }

    initLocalPoint(rIter);
    mTextures[0] = new GameTexture(mTextureSize, mTextureSize);
    mTextures[1] = new GameTexture(mTextureSize, mTextureSize);
    mTexture = mTextures[0];
    mTextureIndex = 0;
    mImage = mTexture->image();
    mLastPointerPoint.zero();
    mLastPlayerPoint.zero();

    MR::connectToSceneNormalMapObj(this);
    MR::validateClipping(this);
    MR::setClippingFar100m(this);
    MR::useStageSwitchWriteDead(this, rIter);
    initSound(4, false);
    makeActorAppeared();
    // The reduce map holds at most 1024 cells a side (a tiny scale made a cell size near 0).
    f32 reduceSize = 800.0f / mCellSize;
    if (!(reduceSize < 1024.0f)) {
        reduceSize = 1024.0f;
    }

    createReduceMap(static_cast< u16 >(reduceSize));
    mDirtyFrames = 2;
}

void SnowFloorTile::initLocalPoint(const TableIter& /*rIter*/) {
    mRight.set(1.0f, 0.0f, 0.0f);
    mUp.set(0.0f, 1.0f, 0.0f);
    mForward.set(0.0f, 0.0f, 1.0f);
    Mtx mtx;
    MR::makeMtxTR(mtx, 0.0f, 0.0f, 0.0f, mRotation.x, mRotation.y, mRotation.z);
    NebulaMtxMultVecSR(mtx, &mRight, &mRight);
    NebulaMtxMultVecSR(mtx, &mUp, &mUp);
    NebulaMtxMultVecSR(mtx, &mForward, &mForward);
}

bool SnowFloorTile::calcLocalPoint(const Vec3f& rPosition, Vec3f* pPoint, f32 heightLimit) {
    Vec3f offset(rPosition - mPosition);
    f32 right = offset.dot(mRight);
    f32 up = offset.dot(mUp);
    f32 forward = offset.dot(mForward);
    f32 height;
    f32 x = -right / mCellSize;
    f32 y = forward / mCellSize;
    x += mTextureSize;
    y += mTextureSize;
    x *= 0.5f;
    y *= 0.5f;
    x /= mTextureSize;
    y /= mTextureSize;
    x = 1.0f - x;
    pPoint->set(x, y, up);

    height = MR::abs(up);

    if (height >= heightLimit) {
        return false;
    }

    if (MR::isInRange(right, -mTextureSize * mCellSize, mTextureSize * mCellSize) &&
        MR::isInRange(forward, -mTextureSize * mCellSize, mTextureSize * mCellSize)) {
        return true;
    }

    return false;
}

void SnowFloorTile::createReduceMap(u16 size) {
    u32 count = size * size;
    mReduceMapSize = size;
    mReduceMap = new f32[count];

    for (u32 i = 0; i < count; i++) {
        mReduceMap[i] = -1.0f;
    }

    u32 samples = 0;
    f32 total = 0.0f;
    f32 center = size / 2;

    for (s32 y = 0; y < size; y++) {
        for (s32 x = 0; x < size; x++) {
            Vec2f offset(x - center, y - center);
            f32 distance = offset.length();

            if (distance >= size / 2) {
                continue;
            }

            f32 value = distance + 0.5f * (distance * MR::getRandom());
            samples++;
            total += value;
            mReduceMap[y * size + x] = value;
        }
    }

    f32 average = total / samples;

    for (s32 y = 0; y < size; y++) {
        for (s32 x = 0; x < size; x++) {
            if (mReduceMap[y * size + x] < 0.0f) {
                mReduceMap[y * size + x] = 0.0f;
            } else {
                mReduceMap[y * size + x] -= average;
                mReduceMap[y * size + x] /= 0.2f * average;
            }
        }
    }
}

void SnowFloorTile::digPlayerWalk(const Vec3f& rPoint) {
    Vec2f point(rPoint.x, rPoint.y);
    MR::setPlayerWalkingResist(0.1f);
    doMove(mLastPlayerPoint, point, 20.0f / static_cast< f32 >(mCellSize), ::cPlayerDigAmount);
    mLastPlayerPoint = point;
}

void SnowFloorTile::digPointer(const Vec3f& rPoint) {
    Vec2f point(rPoint.x, rPoint.y);
    s32 volume = doMove(mLastPointerPoint, point, 50.0f / static_cast< f32 >(mCellSize), ::cPointerDigAmount) / 200.0f;

    if (volume > 100) {
        volume = 100;
    }

    MR::startLevelSound(this, "SE_OJ_LV_SNOW_DIG", volume);
    mLastPointerPoint = point;
}

void SnowFloorTile::control() {
    Vec3f position(*MR::getPlayerPos());
    Vec3f localPoint;
    bool changed = false;

    if (calcLocalPoint(position, &localPoint, 20.0f)) {
        digPlayerWalk(localPoint);
        changed = true;
    }

    if (MR::isPlayerSwingAction()) {
        if (doErase(*MR::getPlayerPos(), ::cSwingDigRadius, ::cSwingDigAmount)) {
            MR::startLevelSound(this, "SE_OJ_LV_SNOW_ERASE");
        }

        changed = true;
    } else if (MR::isPlayerHipDropLand()) {
        if (doErase(*MR::getPlayerPos(), ::cHipdropDigRadius, ::cHipdropDigAmount)) {
            MR::startLevelSound(this, "SE_OJ_LV_SNOW_ERASE");
        }

        changed = true;
    }

    if (MR::isHeldFire(0)) {
        position = *MR::getStarPointerWorldPosUsingDepth(0);

        if (calcLocalPoint(position, &localPoint, 20.0f) || MR::isInRange(localPoint.z, -100.0f, 100.0f)) {
            digPointer(localPoint);
            changed = true;
        }
    }

    if (changed) {
        mDirtyFrames = 2;
    }

    createTexture();
}

bool SnowFloorTile::doErase(const Vec3f& rPosition, f32 radius, f32 amount) {
    Vec3f localPoint;

    if (calcLocalPoint(rPosition, &localPoint, 20.0f)) {
        f32 radiusSq = radius * radius;
        f32 height = localPoint.z;

        if (radiusSq < height * height) {
            return false;
        }

        f32 localRadius = MR::sqrt(radiusSq - height * height);
        Vec2f point(localPoint.x, localPoint.y);
        doErase(point, localRadius / mCellSize, amount);
        return true;
    }

    return false;
}

namespace {
    // Floors move on the game thread; a move holds at most one entry per cell of its box.
    MR::GrowBuffer< u16 > sXs;
    MR::GrowBuffer< u16 > sYs;
    MR::GrowBuffer< f32 > sWeights;
}  // namespace

f32 SnowFloorTile::doMove(Vec2f from, Vec2f to, f32 radius, f32 amount) {
    f32 outerRadius;
    f32 removed = 0.0f;
    f32 weightTotal = 0.0f;
    f32 depthTotal = 0.0f;
    u32 minX;
    u32 count = 0;
    Vec2f unitDirection(to - from);
    unitDirection.normalize();
    outerRadius = 1.732051f * radius;
    to.x *= mTextureSize;
    to.y *= mTextureSize;
    s32 left = static_cast< s16 >((s32)(to.x - outerRadius)) - 1;
    s32 top = static_cast< s16 >((s32)(to.y - outerRadius)) - 1;
    s32 right = static_cast< s16 >((s32)(to.x + outerRadius)) + 1;
    s32 bottom = static_cast< s16 >((s32)(to.y + outerRadius)) + 1;
    minX = MR::clamp(left, 0, static_cast< s32 >(mTextureSize));
    u32 minY = MR::clamp(top, 0, static_cast< s32 >(mTextureSize));
    u32 maxX = MR::clamp(right, 0, static_cast< s32 >(mTextureSize));
    u32 maxY = MR::clamp(bottom, 0, static_cast< s32 >(mTextureSize));
    // One entry per cell of the box at most.
    const u32 cells = maxX > minX && maxY > minY ? (maxX - minX) * (maxY - minY) : 0;
    u16* const xs = sXs.room(cells + 1);
    u16* const ys = sYs.room(cells + 1);
    f32* const weights = sWeights.room(cells + 1);
    f32 radiusSq;
    f32 totalSnow = 0.0f;
    outerRadius *= outerRadius;
    radiusSq = radius * radius;

    for (u32 y = minY; y < maxY; y++) {
        u32 row = y * mTextureSize;

        for (u32 x = minX; x < maxX; x++) {
            Vec2f offset(x - to.x, y - to.y);
            f32 distanceSq = offset.squared();

            totalSnow += mSnowDepth[row + x];

            if (distanceSq <= radiusSq) {
                u32 index = row + x;

                if (mSnowDepth[index] > amount) {
                    removed += amount;
                    mSnowDepth[index] -= amount;
                } else {
                    removed += mSnowDepth[index];
                    mSnowDepth[index] = 0.0f;
                }

                depthTotal += mSnowDepth[index];
            } else if (distanceSq <= outerRadius) {
                f32 weight = offset.dot(unitDirection);

                if (weight > 0.0f) {
                    weightTotal += weight;
                    xs[count] = x;
                    ys[count] = y;
                    weights[count] = weight;
                    count++;
                }
            }
        }
    }

    if (MR::isNearZero(totalSnow)) {
        if (MR::isValidSwitchDead(this)) {
            MR::onSwitchDead(this);
        }

        kill();
        return 0.0f;
    }

    if (count == 0) {
        return 0.0f;
    }

    removed /= weightTotal;

    for (u32 i = 0; i < count; i++) {
        mSnowDepth[(ys[i] << mTextureShift) + xs[i]] += removed * weights[i];
    }

    return depthTotal;
}

void SnowFloorTile::doErase(Vec2f point, f32 radius, f32 amount) {
    point.x *= mTextureSize;
    point.y *= mTextureSize;
    u32 minX;
    s32 left = static_cast< s16 >((s32)(point.x - radius)) - 1;
    s32 top = static_cast< s16 >((s32)(point.y - radius)) - 1;
    s32 right = static_cast< s16 >((s32)(point.x + radius)) + 1;
    s32 bottom = static_cast< s16 >((s32)(point.y + radius)) + 1;
    minX = MR::clamp(left, 0, static_cast< s32 >(mTextureSize));
    u32 minY = MR::clamp(top, 0, static_cast< s32 >(mTextureSize));
    u32 maxX = MR::clamp(right, 0, static_cast< s32 >(mTextureSize));
    u32 maxY = MR::clamp(bottom, 0, static_cast< s32 >(mTextureSize));
    f32 radiusSq = radius * radius;

    for (u32 y = minY; y < maxY; y++) {
        u32 row = y * mTextureSize;

        for (u32 x = minX; x < maxX; x++) {
            Vec2f offset(x - point.x, y - point.y);
            f32 distanceSq = offset.squared();

            if (distanceSq <= radiusSq) {
                f32 falloff = 1.0f - distanceSq / radiusSq;
                f32 dig = falloff * falloff;
                dig *= amount;
                u32 index = row + x;

                if (mSnowDepth[index] > dig) {
                    mSnowDepth[index] -= dig;
                } else {
                    mSnowDepth[index] = 0.0f;
                }
            }
        }
    }
}

void SnowFloorTile::createTexture() {
    mTextureIndex = 1 - mTextureIndex;
    mTexture = mTextures[mTextureIndex];
    mImage = mTexture->image();

    if (mDirtyFrames != 0) {
        mDirtyFrames--;
        s32 index = 0;

        for (u32 y = 0; y < mTextureSize; y++) {
            for (u32 x = 0; x < mTextureSize; x++) {
                mImage[index] = MR::clamp(static_cast< s32 >(mSnowDepth[index]), 0, 255);
                index++;
            }
        }

        // The depths as rows of texels (nebula_cooked_texture.h): the renderer reads them as they are.
        RenderTexture::linear(mImage, NEBULA_TEXTURE_LINEAR_L8, mTextureSize, mTextureSize, 1);
    }
}

void SnowFloorTile::attackSensor(HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (MR::isSensorEnemy(pReceiver)) {
        if (MR::isEqualString(pReceiver->mHost->mName, "マリオ炎球")) {
            Vec3f localPoint;
            calcLocalPoint(pReceiver->mPosition, &localPoint, 200.0f);
            doErase(Vec2f(localPoint.x, localPoint.y), 30.0f / mCellSize, ::cHipdropDigAmount);
        } else if (pReceiver->mHost->getBaseMtx()) {
            Vec3f up;
            MR::calcUpVec(&up, pReceiver->mHost);
            Vec3f position(pReceiver->mPosition - up * (pReceiver->mRadius - 20.0f));
            doErase(position, 20.0f, ::cFireballDigAmount);
        }

        mDirtyFrames = 2;
    }
}

void SnowFloorTile::draw() const {
    MR::resetDrawState();
    // runtime/src/render/materials/color_tex_alpha.hlsl: each layer its colour at the snow texture's alpha, drawn
    // where that is above the layer's threshold; blended by alpha, back faces culled, depth tested and written.
    SiteMaterial material;
    TDDraw::setupNative(&material, 1, 1, false);
    material.setShader(NEBULA_SHADER_COLOR_TEX_ALPHA);
    material.mState.cull = NEBULA_CULL_BACK;
    material.setDepth(true, NEBULA_COMPARE_LEQUAL, true);
    material.setTexture(0, mTexture);

    for (u32 i = 0; i < static_cast< u32 >(::sLayerCount); i++) {
        u8 threshold = i * (240 / ::sLayerCount) + 1;
        material.constants[4] = threshold;
        s32 r = ::sColorR + i * ::sColorStepR;
        s32 g = ::sColorG + i * ::sColorStepG;
        s32 b = ::sColorB + i * ::sColorStepB;
        s32 a = ::sColorA + i * ::sColorStepA;
        // Every layer's channels stay within 0..255.
        Color8 color(r, g, b, a);
        material.setColor(0, color.r, color.g, color.b, color.a);
        TypedDraw site(material, nullptr, MR::getCameraViewMtx());
        drawLayer(site, i);
    }
}

void SnowFloorTile::drawLayer(TypedDraw& rDraw, s32 layer) const {
    Vec3f p3, p2, p1, p0;
    p0 = -mRight * mTextureSize * mCellSize - mForward * mTextureSize * mCellSize;
    p1 = mRight * mTextureSize * mCellSize - mForward * mTextureSize * mCellSize;
    p2 = mRight * mTextureSize * mCellSize + mForward * mTextureSize * mCellSize;
    p3 = -mRight * mTextureSize * mCellSize + mForward * mTextureSize * mCellSize;

    for (u32 i = 0; i < ::sStripCount; i++) {
        Vec3f center(mPosition + mUp * (5.0f + ::sLayerHeight * layer));
        VertexPT* vtx = rDraw.vertices< VertexPT >(NEBULA_PRIMITIVE_QUADS, 4);
        *vtx++ = {center.x + p0.x, center.y + p0.y, center.z + p0.z, 0.0f, 0.0f};
        *vtx++ = {center.x + p1.x, center.y + p1.y, center.z + p1.z, 1.0f, 0.0f};
        *vtx++ = {center.x + p2.x, center.y + p2.y, center.z + p2.z, 1.0f, 1.0f};
        *vtx++ = {center.x + p3.x, center.y + p3.y, center.z + p3.z, 0.0f, 1.0f};
    }
}
