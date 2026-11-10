#include "Game/Util/CameraUtil.hpp"
#include "Game/MapObj/SnowFloor.hpp"
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
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 1.4142f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 32.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 1.414f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 1024.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawSetup_f1, "drawSetup.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawSetup_f2, "drawSetup.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLocalPoint_f1, "initLocalPoint.f1", 900.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLocalPoint_f2, "initLocalPoint.f2", 900.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createReduceMap_f1, "createReduceMap.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_createReduceMap_f2, "createReduceMap.f2", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_digPlayerWalk_f1, "digPlayerWalk.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_digPlayerWalk_f2, "digPlayerWalk.f2", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_digPlayerWalk_f3, "digPlayerWalk.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_digPlayerWalk_f4, "digPlayerWalk.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_digPlayerWalk_f5, "digPlayerWalk.f5", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_digPlayerWalk_f6, "digPlayerWalk.f6", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_digPlayerWalk_f7, "digPlayerWalk.f7", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_digPlayerWalk_f8, "digPlayerWalk.f8", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_digPointer_f1, "digPointer.f1", 250.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_digPointer_f2, "digPointer.f2", 250.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_digPointer_f3, "digPointer.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_digPointer_f4, "digPointer.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_digPointer_f5, "digPointer.f5", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_digPointer_f6, "digPointer.f6", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f2, "control.f2", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f3, "control.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_doErase_f1, "doErase.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_doErase_f2, "doErase.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_doMove_f1, "doMove.f1", 1.732051f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateCheckList_f1, "updateCheckList.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateCheckList_f2, "updateCheckList.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateCheckList_f3, "updateCheckList.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCoverSnow_f1, "calcCoverSnow.f1", 1.732051f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f1, "attackSensor.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f2, "attackSensor.f2", 20.0f);

namespace {
    const u16 sTextureSize = 128;
    // log2(sTextureSize): row offset of a cell in the depth arrays.
    const u16 sTextureShift = 7;
    const u16 sStripCount = 16;
    NEBULA_TUNABLE(s16, sLayerCount, 12);
    NEBULA_TUNABLE(s16, sColorR, 220);
    NEBULA_TUNABLE(s16, sColorStepR, -3);
    NEBULA_TUNABLE(s16, sColorG, 220);
    NEBULA_TUNABLE(s16, sColorStepG, -3);
    NEBULA_TUNABLE(s16, sColorB, 250);
    NEBULA_TUNABLE(s16, sColorStepB, -3);
    NEBULA_TUNABLE(s16, sColorA, 64);
    NEBULA_TUNABLE(s16, sColorStepA, 8);
    NEBULA_TUNABLE(s16, sLayerHeight, 6);
    NEBULA_TUNABLE(f32, cPointerDigAmount, 30.0f);
    NEBULA_TUNABLE(f32, cPlayerDigAmount, 600.0f);
    NEBULA_TUNABLE(f32, cFireballDigAmount, 200.0f);
    NEBULA_TUNABLE(f32, cSwingDigAmount, 20.0f);
    NEBULA_TUNABLE(f32, cSwingDigRadius, 100.0f);
    NEBULA_TUNABLE(f32, cHipdropDigAmount, 1000.0f);
    NEBULA_TUNABLE(f32, cHipdropDigRadius, 50.0f);
    NEBULA_TUNABLE(f32, cSnowFallRateBottom, 30.0f);
    NEBULA_TUNABLE(f32, cSnowFallRate, 0.2f);
    NEBULA_TUNABLE(f32, cSnowFallRateQuick, 0.5f);
    NEBULA_TUNABLE(f32, cSnowFallRateFirst, 0.1f);
    NEBULA_TUNABLE(f32, cPlayerWalkResist, 0.5f);
    const u16 sCellSize = 1024 / sTextureSize;
}  // namespace

SnowFloor::SnowFloor(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< SnowFloor >);
}

void SnowFloor::init(const TableIter& rIter) {
    initHitSensor(1);
    MR::addHitSensorEye(this, "eye", 16, cNum_init_f1 * (::sTextureSize * ::sCellSize), Vec3f(0.0f, 0.0f, 0.0f));
    mSnowDepth = new f32[::sTextureSize * ::sTextureSize];
    mBaseDepth = new u8[::sTextureSize * ::sTextureSize];

    for (u32 y = 0; y < ::sTextureSize; y++) {
        for (u32 x = 0; x < ::sTextureSize; x++) {
            mSnowDepth[(y << ::sTextureShift) + x] = cNum_init_f2 + cNum_init_f3 * MR::getRandom();
            mBaseDepth[(y << ::sTextureShift) + x] = (s32)(mSnowDepth[(y << ::sTextureShift) + x]);
            mSnowDepth[(y << ::sTextureShift) + x] += cNum_init_f4;
        }
    }

    initLocalPoint(rIter);
    drawSetup();
    mTextures[0] = new GameTexture(::sTextureSize, ::sTextureSize);
    mTextures[1] = new GameTexture(::sTextureSize, ::sTextureSize);
    mTexture = mTextures[0];
    mTextureIndex = 0;
    mImage = mTexture->image();
    mWalkingResist = 0.0f;
    mResistFrames = 0;
    mCheckCount = 0;

    for (s32 i = 0; i < 8; i++) {
        mCheckList[i] = nullptr;
    }

    mLastPointerPoint.zero();
    mLastPlayerPoint.zero();
    MR::connectToSceneNormalMapObj(this);
    MR::validateClipping(this);
    MR::setClippingTypeSphere(this, cNum_init_f5 * cNum_init_f6);
    MR::setClippingFar100m(this);
    initSound(4, false);
    makeActorAppeared();
    createReduceMap(800 / ::sCellSize);
    mDirtyFrames = 2;
}

void SnowFloor::drawSetup() {
    mTexCoords = new f32[::sStripCount + 1];
    mDirections = new Vec3f[::sStripCount];
    f32 halfStripAngle = mHalfAngle / ::sStripCount;
    f32 angleRange = cNum_drawSetup_f1 * mHalfAngle - cNum_drawSetup_f2 * halfStripAngle;
    u32 i;

    for (i = 0; i < ::sStripCount; i++) {
        mTexCoords[i] = static_cast< f32 >(i) / ::sStripCount;
        Mtx mtx;
        f32 angle = angleRange * (static_cast< f32 >(i) / (::sStripCount - 1)) - mHalfAngle;
        angle += halfStripAngle;
        NebulaMtxRotAxisRad(mtx, &mAxis, angle);
        NebulaMtxMultVecSR(mtx, &mUp, &mDirections[i]);
    }

    mTexCoords[i] = 1.0f;
}

void SnowFloor::initLocalPoint(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    Mtx mtx;
    MR::makeMtxTR(mtx, 0.0f, 0.0f, 0.0f, mRotation.x, mRotation.y, mRotation.z);
    mUp.set(0.0f, 1.0f, 0.0f);
    mAxis.set(0.0f, 0.0f, 1.0f);
    NebulaMtxMultVecSR(mtx, &mUp, &mUp);
    NebulaMtxMultVecSR(mtx, &mAxis, &mAxis);
    f32 halfLength = ::sTextureSize * ::sCellSize;
    mRadius = cNum_initLocalPoint_f1;
    mHalfLength = halfLength;
    mHalfAngle = halfLength / cNum_initLocalPoint_f2;
}

bool SnowFloor::calcLocalPoint(const Vec3f& rPosition, Vec3f* pPoint) {
    Vec3f offset(rPosition - mPosition);
    Vec3f radial;
    MR::vecKillElement(offset, mAxis, &radial);
    f32 radius = radial.length();
    f32 angle = MR::diffAngleSignedHorizontal(mUp, radial, mAxis);
    f32 axisPos = offset.dot(mAxis) / mHalfLength;

    if (MR::isInRange(angle, -mHalfAngle, mHalfAngle) && MR::isInRange(axisPos, -1.0f, 1.0f)) {
        pPoint->set(angle / mHalfAngle, axisPos, radius);
        return true;
    }

    return false;
}

void SnowFloor::createReduceMap(u16 size) {
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

            f32 value = distance + cNum_createReduceMap_f1 * (distance * MR::getRandom());
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
                mReduceMap[y * size + x] /= cNum_createReduceMap_f2 * average;
            }
        }
    }
}

void SnowFloor::digPlayerWalk(const Vec3f& rPoint) {
    Vec2f point(rPoint.x, rPoint.y);

    if (rPoint.z < mRadius - cNum_digPlayerWalk_f1) {
        return;
    }

    if (rPoint.z > cNum_digPlayerWalk_f2 + mRadius) {
        return;
    }

    point.x += 1.0f;
    point.y += 1.0f;
    point.x *= cNum_digPlayerWalk_f3;
    point.y *= cNum_digPlayerWalk_f4;
    point.y = 1.0f - point.y;
    f32 depth = doMove(mLastPlayerPoint, point, cNum_digPlayerWalk_f5 / ::sCellSize, ::cPlayerDigAmount);
    mLastPlayerPoint = point;
    f32 excess = depth - cNum_digPlayerWalk_f6;

    if (excess < 0.0f) {
        if (mResistFrames != 0) {
            MR::setPlayerWalkingResist(mWalkingResist);
            mWalkingResist *= cNum_digPlayerWalk_f7;
        } else {
            MR::setPlayerWalkingResist(0.0f);
        }
    } else {
        f32 resist = MR::clamp(excess / cNum_digPlayerWalk_f8, 0.0f, 1.0f);
        resist *= ::cPlayerWalkResist;
        MR::setPlayerWalkingResist(resist);

        if (mResistFrames != 0) {
            if (resist > mWalkingResist) {
                mWalkingResist = resist;
                mResistFrames = 5;
            }
        } else {
            mWalkingResist = resist;
            mResistFrames = 5;
        }
    }

    if (mResistFrames != 0) {
        mResistFrames--;
    }
}

void SnowFloor::digPointer(const Vec3f& rPoint) {
    Vec2f point(rPoint.x, rPoint.y);

    if (rPoint.z < mRadius - cNum_digPointer_f1) {
        return;
    }

    if (rPoint.z > cNum_digPointer_f2 + mRadius) {
        return;
    }

    point.x += 1.0f;
    point.y += 1.0f;
    point.x *= cNum_digPointer_f3;
    point.y *= cNum_digPointer_f4;
    point.y = 1.0f - point.y;
    s32 volume = doMove(mLastPointerPoint, point, cNum_digPointer_f5 / ::sCellSize, ::cPointerDigAmount) / cNum_digPointer_f6;

    if (volume > 100) {
        volume = 100;
    }

    MR::startLevelSound(this, "SE_OJ_LV_SNOW_DIG", volume);
    mLastPointerPoint = point;
}

void SnowFloor::control() {
    Vec3f position(*MR::getPlayerPos());
    Vec3f localPoint;
    bool changed = false;
    updateCheckList();

    if (calcLocalPoint(position, &localPoint)) {
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

    if (MR::isStarPointerInScreen(0)) {
        position = *MR::getStarPointerWorldPosUsingDepth(0);

        if (calcLocalPoint(position, &localPoint)) {
            digPointer(localPoint);
            changed = true;
        }
    }

    if (MR::isStarPointerInScreen(1)) {
        position = *MR::getStarPointerWorldPosUsingDepth(1);

        if (calcLocalPoint(position, &localPoint)) {
            digPointer(localPoint);
            changed = true;
        }
    }

    for (u32 y = 0; y < ::sTextureSize; y++) {
        u32 row = y << ::sTextureShift;

        for (u32 x = 0; x < ::sTextureSize; x++) {
            u32 index = row + x;

            if (mSnowDepth[index] < cNum_control_f1) {
                mSnowDepth[index] += ::cSnowFallRateFirst * MR::getRandom();
            } else if (mSnowDepth[index] < ::cSnowFallRateBottom) {
                mSnowDepth[index] += ::cSnowFallRateQuick * MR::getRandom();
            } else {
                f32 limit = cNum_control_f2 + mBaseDepth[index];
                f32 deficit = limit - mSnowDepth[index];

                if (deficit > 0.0f) {
                    deficit /= limit - ::cSnowFallRateBottom;
                    mSnowDepth[index] += deficit * (::cSnowFallRate * MR::getRandom());
                }
            }

            mSnowDepth[index] = MR::clamp(mSnowDepth[index], 0.0f, cNum_control_f3 + mBaseDepth[index]);
            changed = true;
        }
    }

    if (changed) {
        mDirtyFrames = 2;
    }

    createTexture();
}

bool SnowFloor::doErase(const Vec3f& rPosition, f32 radius, f32 amount) {
    Vec3f localPoint;

    if (calcLocalPoint(rPosition, &localPoint)) {
        f32 radiusSq = radius * radius;
        f32 height = localPoint.z - mRadius;

        if (radiusSq < height * height) {
            return false;
        }

        f32 localRadius = MR::sqrt(radiusSq - height * height);
        Vec2f point(localPoint.x, localPoint.y);
        point.x += 1.0f;
        point.y += 1.0f;
        point.x *= cNum_doErase_f1;
        point.y *= cNum_doErase_f2;
        point.y = 1.0f - point.y;
        doErase(point, localRadius / ::sCellSize, amount);
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

f32 SnowFloor::doMove(Vec2f from, Vec2f to, f32 radius, f32 amount) {
    f32 removed = 0.0f;
    f32 weightTotal = 0.0f;
    f32 depthTotal = 0.0f;
    u32 minX;
    u32 count = 0;
    Vec2f unitDirection(to - from);
    unitDirection.normalize();
    f32 outerRadius = cNum_doMove_f1 * radius;
    to.x *= ::sTextureSize;
    to.y *= ::sTextureSize;
    s32 left = static_cast< s16 >((s32)(to.x - outerRadius)) - 1;
    s32 top = static_cast< s16 >((s32)(to.y - outerRadius)) - 1;
    s32 right = static_cast< s16 >((s32)(to.x + outerRadius)) + 1;
    s32 bottom = static_cast< s16 >((s32)(to.y + outerRadius)) + 1;
    minX = MR::clamp(left, 0, static_cast< s32 >(::sTextureSize));
    u32 minY = MR::clamp(top, 0, static_cast< s32 >(::sTextureSize));
    u32 maxX = MR::clamp(right, 0, static_cast< s32 >(::sTextureSize));
    u32 maxY = MR::clamp(bottom, 0, static_cast< s32 >(::sTextureSize));
    // One entry per cell of the box at most.
    const u32 cells = maxX > minX && maxY > minY ? (maxX - minX) * (maxY - minY) : 0;
    u16* const xs = sXs.room(cells + 1);
    u16* const ys = sYs.room(cells + 1);
    f32* const weights = sWeights.room(cells + 1);
    f32 outerRadiusSq = outerRadius * outerRadius;
    f32 radiusSq = radius * radius;

    for (u32 y = minY; y < maxY; y++) {
        u32 row = y * ::sTextureSize;

        for (u32 x = minX; x < maxX; x++) {
            Vec2f offset(x - to.x, y - to.y);
            f32 distanceSq = offset.squared();

            if (distanceSq <= radiusSq) {
                u32 index = row + x;
                depthTotal += mSnowDepth[index];

                if (mSnowDepth[index] > amount) {
                    removed += amount;
                    mSnowDepth[index] -= amount;
                } else {
                    removed += mSnowDepth[index];
                    mSnowDepth[index] = 0.0f;
                }
            } else if (distanceSq <= outerRadiusSq) {
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

    if (count == 0) {
        return 0.0f;
    }

    removed /= weightTotal;

    for (u32 i = 0; i < count; i++) {
        mSnowDepth[(ys[i] << ::sTextureShift) + xs[i]] += removed * weights[i];
    }

    return depthTotal;
}

void SnowFloor::doErase(Vec2f point, f32 radius, f32 amount) {
    point.x *= ::sTextureSize;
    point.y *= ::sTextureSize;
    u32 minX;
    s32 left = static_cast< s16 >((s32)(point.x - radius)) - 1;
    s32 top = static_cast< s16 >((s32)(point.y - radius)) - 1;
    s32 right = static_cast< s16 >((s32)(point.x + radius)) + 1;
    s32 bottom = static_cast< s16 >((s32)(point.y + radius)) + 1;
    minX = MR::clamp(left, 0, static_cast< s32 >(::sTextureSize));
    u32 minY = MR::clamp(top, 0, static_cast< s32 >(::sTextureSize));
    u32 maxX = MR::clamp(right, 0, static_cast< s32 >(::sTextureSize));
    u32 maxY = MR::clamp(bottom, 0, static_cast< s32 >(::sTextureSize));
    f32 radiusSq = radius * radius;

    for (u32 y = minY; y < maxY; y++) {
        u32 row = y * ::sTextureSize;

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

void SnowFloor::createTexture() {
    mTextureIndex = 1 - mTextureIndex;
    mTexture = mTextures[mTextureIndex];
    mImage = mTexture->image();

    if (mDirtyFrames != 0) {
        mDirtyFrames--;
        s32 index = 0;

        for (u32 y = 0; y < ::sTextureSize; y++) {
            for (u32 x = 0; x < ::sTextureSize; x++) {
                mImage[index] = MR::clamp(static_cast< s32 >(mSnowDepth[index]), 0, 255);
                index++;
            }
        }

        // The depths as rows of texels (nebula_cooked_texture.h): the renderer reads them as they are.
        RenderTexture::linear(mImage, NEBULA_TEXTURE_LINEAR_L8, ::sTextureSize, ::sTextureSize, 1);
    }
}

void SnowFloor::addCheckList(HitSensor* pSensor) {
    for (u32 i = 0; i < mCheckCount; i++) {
        if (mCheckList[i] == pSensor) {
            return;
        }

        if (mCheckList[i]->mHost == pSensor->mHost) {
            return;
        }
    }

    mCheckList.at(mCheckCount) = pSensor;
    mCheckCount++;
}

void SnowFloor::updateCheckList() {
    bool discovered = false;

    if (mCheckCount != 0) {
        HitSensor* sensor = mCheckList[0];
        Vec3f localPoint;

        if (calcLocalPoint(sensor->mPosition, &localPoint)) {
            Vec2f point(localPoint.x, localPoint.y);
            point.x += 1.0f;
            point.y += 1.0f;
            point.x *= cNum_updateCheckList_f1;
            point.y *= cNum_updateCheckList_f2;
            point.y = 1.0f - point.y;
            f32 cellSize = ::sCellSize;
            f32 radius = sensor->mRadius;
            f32 cover = calcCoverSnow(point, radius / cellSize);
            f32 sensorRadius = sensor->mRadius;
            f32 threshold = cNum_updateCheckList_f3 * sensorRadius;

            if (cover < threshold) {
                discovered = MR::sendArbitraryMsg(ACTMES_NOTIFY_DISCOVER_SNOW, sensor, getSensor("eye"));
            }
        }

        mCheckCount--;

        for (u32 i = 0; i < mCheckCount; i++) {
            mCheckList[i] = mCheckList[i + 1];
        }

        if (!discovered) {
            mCheckList.at(mCheckCount) = sensor;
            mCheckCount++;
        }
    }
}

f32 SnowFloor::calcCoverSnow(const Vec2f& rPoint, f32 radius) {
    f32 total = 0.0f;
    Vec2f point(rPoint);
    f32 outerRadius = cNum_calcCoverSnow_f1 * radius;
    point.x *= ::sTextureSize;
    point.y *= ::sTextureSize;
    s32 left = static_cast< s16 >((s32)(point.x - outerRadius)) - 1;
    s32 top = static_cast< s16 >((s32)(point.y - outerRadius)) - 1;
    s32 right = static_cast< s16 >((s32)(point.x + outerRadius)) + 1;
    s32 bottom = static_cast< s16 >((s32)(point.y + outerRadius)) + 1;
    u32 minX = MR::clamp(left, 0, static_cast< s32 >(::sTextureSize));
    u32 minY = MR::clamp(top, 0, static_cast< s32 >(::sTextureSize));
    u32 maxX = MR::clamp(right, 0, static_cast< s32 >(::sTextureSize));
    u32 maxY = MR::clamp(bottom, 0, static_cast< s32 >(::sTextureSize));
    f32 radiusSq = radius * radius;

    for (u32 y = minY; y < maxY; y++) {
        u32 row = y * ::sTextureSize;

        for (u32 x = minX; x < maxX; x++) {
            Vec2f offset(x - point.x, y - point.y);

            if (offset.squared() <= radiusSq) {
                total += mSnowDepth[row + x];
            }
        }
    }

    return total;
}

void SnowFloor::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorEnemy(pReceiver)) {
        return;
    }

    if (MR::isEqualString(pReceiver->mHost->mName, "マリオ炎球")) {
        doErase(pReceiver->mPosition, pReceiver->mRadius, ::cFireballDigAmount);
    } else {
        for (u32 i = 0; i < mCheckCount; i++) {
            if (mCheckList[i] == pReceiver) {
                return;
            }

            if (mCheckList[i]->mHost == pReceiver->mHost) {
                return;
            }
        }

        if (MR::sendArbitraryMsg(ACTMES_ASK_HIDDEN_BY_SNOW, pReceiver, pSender)) {
            addCheckList(pReceiver);
            return;
        }

        if (pReceiver->mHost->getBaseMtx()) {
            Vec3f up;
            MR::calcUpVec(&up, pReceiver->mHost);
            Vec3f position(pReceiver->mPosition - up * (pReceiver->mRadius - cNum_attackSensor_f1));
            doErase(position, cNum_attackSensor_f2, ::cFireballDigAmount);
        }
    }

    mDirtyFrames = 2;
}

void SnowFloor::draw() const {
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

void SnowFloor::drawLayer(TypedDraw& rDraw, s32 layer) const {
    Vec3f p3, p2, p1, p0;
    Vec3f axis(mAxis);
    Vec3f tangent, cross;
    f32 halfWidth = (static_cast< f32 >(::sTextureSize) * ::sCellSize) / ::sStripCount;
    f32 halfLength = static_cast< f32 >(::sTextureSize) * ::sCellSize;

    for (u32 i = 0; i < ::sStripCount; i++) {
        Vec3f center(mPosition + mDirections[i] * (mRadius + ::sLayerHeight * layer));
        const Vec3f& direction = mDirections[i];
        tangent.cross(direction, axis);
        MR::normalizeOrZero(&tangent);
        cross.cross(tangent, direction);
        MR::normalizeOrZero(&cross);
        tangent = -tangent;

        if (i != 0) {
            p0 = p1;
            p3 = p2;
        } else {
            p0 = center - tangent * halfWidth + cross * halfLength;
            p3 = center - tangent * halfWidth - cross * halfLength;
        }

        p1 = center + tangent * halfWidth + cross * halfLength;
        p2 = center + tangent * halfWidth - cross * halfLength;
        VertexPT* vtx = rDraw.vertices< VertexPT >(NEBULA_PRIMITIVE_QUADS, 4);
        *vtx++ = {p0.x, p0.y, p0.z, mTexCoords[i], 0.0f};
        *vtx++ = {p1.x, p1.y, p1.z, mTexCoords[i + 1], 0.0f};
        *vtx++ = {p2.x, p2.y, p2.z, mTexCoords[i + 1], 1.0f};
        *vtx++ = {p3.x, p3.y, p3.z, mTexCoords[i], 1.0f};
    }
}
