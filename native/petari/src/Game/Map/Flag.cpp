#include <nebula_game_api.h>
#include "Game/Map/Flag.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Ride/SwingRopePoint.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Render/GameTexture.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sDefaultPointIntervalU = 40.0f;
    const f32 sDefaultPointIntervalV = 40.0f;
    const f32 sDefaultGravity = 0.1f;
    const f32 sDefaultFrictionRateMin = 0.6f;
    const f32 sDefaultFrictionRateMax = 1.0f;
    const f32 sWindTimeSpeed = 5.0f;
    const f32 sWindPosRateU = 10.0f;
    const f32 sWindPosRateV = 10.0f;
    const f32 sDefaultWindAccelRate = 10.0f;
    const f32 sDefaultWindAccelMin = 0.1f;
    const f32 sDefaultRandomWindAccelMin = 1.0f;
    const f32 sDefaultRandomWindAccelMax = 4.0f;
    const s32 sRandomWaitTimeMax = 2;
    const f32 sLightBorderMin = -1.0f;
    const f32 sLightBorderMax = 0.5f;
    const f32 sLightColorMinNear = 200.0f;
    const f32 sLightColorMinFar = 120.0f;
    const f32 sLightDistanceNear = 500.0f;
    const f32 sLightDistanceFar = 1000.0f;
    const f32 sDefaultAlphaDistanceNear = 1000.0f;
    const f32 sDefaultAlphaDistanceFar = 2000.0f;
    const f32 sStickWidth = 5.0f;
    const f32 sClippingRadiusOffset = 300.0f;
    const s32 sSeStepMin = 36;
    const s32 sSeStepMax = 66;
    const s32 sRandomWaitTimeMin = 0;
};  // namespace

namespace {
    const NebulaColor8 sStickColors[] = {{0x00, 0xC8, 0x00, 0x00}, {0x00, 0xDC, 0x00, 0x00}, {0x00, 0xB4, 0x00, 0x00}, {0x00, 0xC8, 0x00, 0x00}};
};  // namespace

Flag::Flag(const char* pName)
    : LiveActor(pName), mObjName(), mDisableLighting(), mNumPointsV(10), mNumPointsU(10), mPointIntervalU(::sDefaultPointIntervalU),
      mPointIntervalV(::sDefaultPointIntervalV), mFixPoints(), mUp(0.0f, 1.0f, 0.0f), mClipCenter(0.0f, 0.0f, 0.0f), mBasePos(), mBaseMtx(),
      mStickLength(), mWavePhase(), mSide(0.0f, 0.0f, 1.0f), mFront(0.0f, 0.0f, 0.0f), mIsVerticalFlag(), mWaitTime(),
      mGravityScale(::sDefaultGravity), mFrictionRateMin(::sDefaultFrictionRateMin), mFrictionRateMax(::sDefaultFrictionRateMax),
      mWindAccelRate(::sDefaultWindAccelRate), mWindAccelMin(::sDefaultWindAccelMin), mRandomWindAccelMin(::sDefaultRandomWindAccelMin),
      mRandomWindAccelMax(::sDefaultRandomWindAccelMax), mLightColorMin(::sLightColorMinFar), mColors(), mTexST(), mTexture(), mUseAlpha(),
      mAlpha(0xFF), mAlphaDistanceNear(::sDefaultAlphaDistanceNear), mAlphaDistanceFar(::sDefaultAlphaDistanceFar), mSeStep(), mDisableSound() {
    lumacore::setClass(this, lumacore::kClassId< Flag >);
}

void Flag::init(const TableIter& rIter) {
    MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_Flag);

    if (MR::isValidInfo(rIter)) {
        MR::initDefaultPos(this, rIter);
        MR::getPlacementArg1NoInit(rIter, &mStickLength);
        MR::getObjectName(&mObjName, rIter);
        if (MR::isEqualString(mObjName, "FlagKoopaCastle")) {
            mPointIntervalU = 1000.0f / mNumPointsU;
            mPointIntervalV = 500.0f / (mNumPointsV - 1);
        } else if (MR::isEqualString(mObjName, "FlagKoopaA")) {
            mPointIntervalU = 450.0f / mNumPointsU;
            mPointIntervalV = 275.0f / (mNumPointsV - 1);
        } else if (MR::isEqualString(mObjName, "FlagKoopaB")) {
            mPointIntervalU = 450.0f / mNumPointsU;
            mPointIntervalV = 112.5f / (mNumPointsV - 1);
        } else if (MR::isEqualString(mObjName, "FlagPeachCastleA")) {
            mNumPointsV = 5;
            mNumPointsU = 6;
            mPointIntervalU = 170.0f / mNumPointsU;
            mPointIntervalV = 145.0f / (mNumPointsV - 1);
            mAlphaDistanceNear = 200.0f;
            mAlphaDistanceFar = 500.0f;
            mIsVerticalFlag = true;
            mGravityScale = 0.5f;
            mFrictionRateMin = 0.85f;
            mFrictionRateMax = 1.0f;
            mWindAccelRate = 0.0f;
            mWindAccelMin = 0.01f;
            mRandomWindAccelMin = 0.5f;
            mRandomWindAccelMax = 1.5f;
            mDisableSound = true;
        } else if (MR::isEqualString(mObjName, "FlagPeachCastleB")) {
            mNumPointsV = 5;
            mNumPointsU = 5;
            mPointIntervalU = 500.0f / mNumPointsU;
            mPointIntervalV = 400.0f / (mNumPointsV - 1);
        } else if (MR::isEqualString(mObjName, "FlagPeachCastleC")) {
            mNumPointsV = 5;
            mNumPointsU = 5;
            mPointIntervalU = 500.0f / mNumPointsU;
            mPointIntervalV = 400.0f / (mNumPointsV - 1);
        } else if (MR::isEqualString(mObjName, "FlagRaceA")) {
            mDisableSound = true;
        }
        Vec3f front;
        MR::calcActorAxis(&front, &mUp, &mSide, this);
    }

    if (MR::isEqualString(mObjName, "FlagSurfing")) {
        mDisableLighting = true;
        mUseAlpha = true;
    }

    mFixPoints = new FixPoints[mNumPointsV];

    for (s32 idxV = 0; idxV < mNumPointsV; idxV++) {
        Vec3f pos = mUp;
        pos.scale(mStickLength + mPointIntervalV * ((mNumPointsV - 1) - idxV));
        pos.add(mPosition);
        mFixPoints[idxV].mPos.set(pos);

        mFixPoints[idxV].mPoints = new SwingRopePoint*[mNumPointsU];
        for (s32 idxU = 0; idxU < mNumPointsU; idxU++) {
            Vec3f posU = pos;
            if (mIsVerticalFlag) {
                posU.add(mGravity * (mPointIntervalU * (idxU + 1)));
            }

            mFixPoints[idxV].mPoints[idxU] = new SwingRopePoint(posU);
        }
    }

    mColors = new Color8[mNumPointsV * (mNumPointsU + 1)];
    for (s32 idx = 0; idx < mNumPointsV * (mNumPointsU + 1); idx++) {
        mColors[idx].set(0xFF, 0xFF, 0xFF, 0xFF);
    }

    mTexST = new f32[mNumPointsV * (mNumPointsU + 1) * 2];
    s32 idx = 0;
    for (s32 idxV = 0; idxV < mNumPointsV; idxV++) {
        // One row is the top of the picture.
        f32 texV = mNumPointsV > 1 ? static_cast< f32 >(idxV) / static_cast< f32 >(mNumPointsV - 1) : 0.0f;
        mTexST[idx + 0] = 0.0f;
        mTexST[idx + 1] = texV;
        idx += 2;

        for (s32 idxU = 0; idxU < mNumPointsU; idxU++) {
            mTexST[idx + 0] = static_cast< f32 >(idxU + 1) / static_cast< f32 >(mNumPointsU);
            mTexST[idx + 1] = texV;
            idx += 2;
        }
    }

    if (mObjName != nullptr) {
        mTexture = new GameTexture(MR::loadTexFromArc(mObjName));
    } else {
        mTexture = new GameTexture(MR::loadTexFromArc("Flag", "Flag.bti"));
    }

    initSound(4, false);

    if (MR::isEqualString(mObjName, "FlagKoopaCastle")) {
        mClipCenter = mUp * 500.0f + mPosition;
        MR::setClippingTypeSphere(this, 500.0f + ::sClippingRadiusOffset, &mClipCenter);
    } else if (mStickLength > 0.0f) {
        if (mBasePos == nullptr && mBaseMtx == nullptr) {
            mClipCenter = mUp * (mStickLength * 0.5f) + mPosition;
            MR::setClippingTypeSphere(this, mStickLength / 2 + ::sClippingRadiusOffset, &mClipCenter);
        } else {
            MR::setClippingTypeSphere(this, mStickLength + ::sClippingRadiusOffset);
        }
    }

    if (MR::tryRegisterDemoCast(this, rIter)) {
        makeActorDead();
    } else {
        MR::registerDemoSimpleCastAll(this);
        makeActorAppeared();
    }
}

void Flag::appear() {
    LiveActor::appear();

    for (s32 idxU = 0; idxU < mNumPointsU; idxU++) {
        for (s32 idxV = 0; idxV < mNumPointsV; idxV++) {
            SwingRopePoint* point = mFixPoints[idxV].mPoints[idxU];
            point->setPosAndAxis(mFixPoints[idxV].mPos, point->mSide, point->mUp, point->mFront);
        }
    }

    MR::startSound(this, "SE_OJ_FLAG_APPEAR");
}

void Flag::setInfoPos(const char* pObjName, const Vec3f* pPos, const Vec3f& rSide, f32 stickLength, f32 width, f32 height, s32 numPointsU,
                      s32 numPointsV) {
    mObjName = pObjName;
    mBasePos = pPos;
    mSide.set(rSide);
    mPosition.set(*mBasePos);
    mStickLength = stickLength;

    if (numPointsU > 0) {
        mNumPointsU = numPointsU;
    }

    if (numPointsV > 0) {
        mNumPointsV = numPointsV;
    }

    if (width > 0.0f) {
        mPointIntervalU = width / mNumPointsU;
    }

    // One row has no height between rows.
    if (height > 0.0f && mNumPointsV > 1) {
        mPointIntervalV = height / (mNumPointsV - 1);
    }
}

void Flag::movement() {
    if (!mDisableSound) {
        if (mSeStep <= 0) {
            MR::startSound(this, "SE_OJ_FLAG");
            mSeStep = MR::getRandom(::sSeStepMin, ::sSeStepMax);
        } else {
            mSeStep--;
        }
    }

    mWavePhase += ::sWindTimeSpeed;
    updateFlag();
}

void Flag::updateFlag() {
    NebulaVecCrossProduct(&mSide, &mUp, &mFront);
    if (MR::isNearZero(mFront)) {
        mFront.set(mSide);
    } else {
        MR::normalize(&mFront);
    }

    f32 camDist = mPosition.distance(MR::getCamPos());
    if (!mDisableLighting) {
        if (camDist < ::sLightDistanceNear) {
            mLightColorMin = ::sLightColorMinNear;
        } else if (camDist >= ::sLightDistanceFar) {
            mLightColorMin = ::sLightColorMinFar;

        } else {
            f32 d = (camDist - ::sLightDistanceNear) / (::sLightDistanceFar - ::sLightDistanceNear);
            mLightColorMin = d * ::sLightColorMinFar + (1.0f - d) * ::sLightColorMinNear;
        }
    }

    if (mUseAlpha) {
        if (camDist < mAlphaDistanceNear) {
            mAlpha = 0;
        } else if (camDist > mAlphaDistanceFar) {
            mAlpha = 255;
        } else {
            mAlpha = (s32)(((camDist - mAlphaDistanceNear) / (mAlphaDistanceFar - mAlphaDistanceNear)) * 255);
        }
    }

    if (mBasePos != nullptr) {
        mPosition.set(*mBasePos);
        for (s32 idxV = 0; idxV < mNumPointsV; idxV++) {
            Vec3f pos = mUp;
            pos.scale(mStickLength + mPointIntervalV * ((mNumPointsV - 1) - idxV));
            pos.add(*getPosition());
            mFixPoints[idxV].mPos.set(pos);
        }
    }

    if (mBaseMtx != nullptr) {
        mBaseMtx->getTransInline2(mPosition);
        mBaseMtx->getYDir2(mUp);

        for (s32 idxV = 0; idxV < mNumPointsV; idxV++) {
            Vec3f pos = mUp;
            pos.scale(mStickLength + mPointIntervalV * ((mNumPointsV - 1) - idxV));
            pos.add(*getPosition());
            mFixPoints[idxV].mPos.set(pos);
        }
    }

    // Add gravity and side wind
    Vec3f grav = *getGravity();
    grav.scale(mGravityScale);
    for (s32 idxV = 0; idxV < mNumPointsV; idxV++) {
        for (s32 idxU = 0; idxU < mNumPointsU; idxU++) {
            SwingRopePoint* point = mFixPoints[idxV].mPoints[idxU];
            point->addAccel(grav);

            Vec3f side = mSide;
            f32 wave = MR::sinDegree(mWavePhase + ::sWindPosRateU * idxU + ::sWindPosRateV * idxV);
            if (wave < 0.0f) {
                wave *= -1.0f;
            }
            side.scale(mWindAccelMin + wave * mWindAccelRate);
            point->addAccel(side);
        }
    }

    if (mIsVerticalFlag) {
        if (mWaitTime <= 0) {
            // Add random wind accel at single point
            s32 pointV = MR::getRandom(0, mNumPointsV);
            s32 pointU = MR::getRandom(0, mNumPointsU);
            // Drawn z, then x: the console's right-to-left order of the arguments (as the Windows build's), on every platform.
            const f32 windZ = MR::getRandom(-1.0f, 1.0f);
            const f32 windX = MR::getRandom(-1.0f, 1.0f);
            Vec3f wind(windX, 0.0f, windZ);
            MR::vecKillElement(wind, mUp, &wind);
            wind.scale(MR::getRandom(mRandomWindAccelMin, mRandomWindAccelMax));
            mFixPoints[pointV].mPoints[pointU]->addAccel(wind);
            mWaitTime = MR::getRandom(::sRandomWaitTimeMin, ::sRandomWaitTimeMax);
        } else {
            mWaitTime--;
        }

    } else {
        // Add random wind accel across whole flag
        for (s32 idxV = 0; idxV < mNumPointsV; idxV++) {
            // Drawn z, then x: the console's right-to-left order of the arguments (as the Windows build's), on every platform.
            const f32 windZ = MR::getRandom(-1.0f, 1.0f);
            const f32 windX = MR::getRandom(-1.0f, 1.0f);
            Vec3f wind(windX, 0.0f, windZ);
            wind.scale(MR::getRandom(mRandomWindAccelMin, mRandomWindAccelMax));
            mFixPoints[idxV].mPoints[0]->addAccel(wind);
        }
    }

    // restrict in horizontal direction
    for (s32 idxU = 0; idxU < mNumPointsU; idxU++) {
        Vec3f* pos = &mFixPoints[0].mPoints[idxU]->mPosition;
        Vec3f* vel = &mFixPoints[0].mPoints[idxU]->mVelocity;
        for (s32 idxV = 1; idxV < mNumPointsV; idxV++) {
            SwingRopePoint* point = mFixPoints[idxV].mPoints[idxU];
            point->restrict(*pos, mPointIntervalV, vel);
            pos = &point->mPosition;
            vel = &point->mVelocity;
        }
    }

    // restrict in vertical direction
    for (s32 idxV = 0; idxV < mNumPointsV; idxV++) {
        Vec3f* pos = &mFixPoints[idxV].mPos;
        for (s32 idxU = 0; idxU < mNumPointsU; idxU++) {
            SwingRopePoint* point = mFixPoints[idxV].mPoints[idxU];
            point->restrict(*pos, mPointIntervalU, nullptr);
            pos = &point->mPosition;
        }
    }

    for (s32 idxU = 0; idxU < getNumPointsU(); idxU++) {
        // One column is the fixed end of the cloth.
        const f32 along = mNumPointsU > 1 ? static_cast< f32 >(idxU) / static_cast< f32 >(mNumPointsU - 1) : 0.0f;
        f32 friction = mFrictionRateMin + (1.0f - along) * (mFrictionRateMax - mFrictionRateMin);

        for (s32 idxV = 0; idxV < getNumPointsV(); idxV++) {
            SwingRopePoint* point = mFixPoints[idxV].mPoints[idxU];
            point->updatePos(friction);

            if (!mDisableLighting) {
                f32 lightColor = 255.0f * ((point->mUp.dot(mFront) - ::sLightBorderMin) / (::sLightBorderMax - ::sLightBorderMin));
                u8 color = (s32)(MR::clamp(lightColor, mLightColorMin, 255.0f));
                mColors[idxV * (mNumPointsU + 1) + idxU + 1].set(color, color, color, 0xFF);
            }
        }
    }
}

void Flag::draw() const {
    if (!MR::isValidDraw(this)) {
        return;
    }

    const SiteMaterial material = loadMaterial();
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());

    u8 indexA = 0;
    u8 indexB = mNumPointsU + 1;
    for (s32 idxV = 0; idxV < mNumPointsV - 1; idxV++) {
        const Vec3f& posA = mFixPoints[idxV + 0].mPos;
        const Vec3f& posB = mFixPoints[idxV + 1].mPos;
        // Each vertex's colour and texture coordinate are its entries of mColors and mTexST.
        VertexPCT* vtx = site.vertices< VertexPCT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, (mNumPointsU + 1) * 2);
        {
            *vtx++ = vertexOf(posB, indexB);
            *vtx++ = vertexOf(posA, indexA);
            indexB++;
            indexA++;

            for (s32 idxU = 0; idxU < mNumPointsU; idxU++) {
                const Vec3f& posA = mFixPoints[idxV].mPoints[idxU]->mPosition;
                const Vec3f& posB = mFixPoints[idxV + 1].mPoints[idxU]->mPosition;
                *vtx++ = vertexOf(posB, indexB);
                *vtx++ = vertexOf(posA, indexA);

                indexB++;
                indexA++;
            }
        }
    }

    if (mStickLength > 0.0f) {
        const Vec2f stickCoords[] = {{1.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}, {1.0f, 0.0f}};

        // The stick's colours are ::sStickColors' entries, its texture coordinate mTexST's first.
        VertexPCT* vtx = site.vertices< VertexPCT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 8);

        f32 length = mStickLength + mPointIntervalV * (mNumPointsV - 1);
        for (s32 idx = 0; idx < 4; idx++) {
            // The offset goes on twice: the stick is twice sStickWidth out.
            Vec3f offset(stickCoords[idx].x, 0.0f, stickCoords[idx].y);
            offset.scale(::sStickWidth);
            Vec3f posA = mUp * length;
            posA.add(offset);
            posA.add(offset);
            posA.add(mPosition);
            Vec3f posB = mPosition;
            posB.add(offset);
            posB.add(offset);

            *vtx++ = {posA.x, posA.y, posA.z, Rgba8(::sStickColors[idx].r, ::sStickColors[idx].g, ::sStickColors[idx].b, ::sStickColors[idx].a), mTexST[0], mTexST[1]};
            *vtx++ = {posB.x, posB.y, posB.z, Rgba8(::sStickColors[idx].r, ::sStickColors[idx].g, ::sStickColors[idx].b, ::sStickColors[idx].a), mTexST[0], mTexST[1]};
        }
    }
}

VertexPCT Flag::vertexOf(const Vec3f& rPos, u8 index) const {
    return VertexPCT{rPos.x, rPos.y, rPos.z, Rgba8(mColors[index]), mTexST[2 * index], mTexST[2 * index + 1]};
}

SiteMaterial Flag::loadMaterial() const {

    // runtime/src/render/materials/tex_modulate.hlsl: the texture times the vertex colour, its alpha times mAlpha;
    // blended by alpha where above zero, depth tested (written when opaque), no culling, no fog.
    SiteMaterial material;
    material.init(NEBULA_SHADER_TEX_MODULATE, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL,
                       mAlpha == 0xFF, NEBULA_CULL_NONE);
    material.constants[0] = mAlpha / 255.0f;
    material.setTexture(0, mTexture);
    return material;
}
