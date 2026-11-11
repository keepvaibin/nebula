#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Ride/PlantStalk.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/MapObj/PlantPoint.hpp"
#include "Game/MapObj/PlantRailInfo.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcPlayerCoord_f1, "calcPlayerCoord.f1", 100000000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcPlayerCoord_f2, "calcPlayerCoord.f2", 15.0f);

namespace {
    NEBULA_TUNABLE(f32, sGrowSpeedMin, 15.0f);
    NEBULA_TUNABLE(f32, sGrowSpeedMax, 100.0f);
    NEBULA_TUNABLE(f32, sGrowSpeedFriction, 0.95f);
    NEBULA_TUNABLE(f32, sGrowAccelMin, 15.0f);
    NEBULA_TUNABLE(f32, sGrowAccelMax, 30.0f);
    NEBULA_TUNABLE(s32, sGrowAccelTimeMin, 5);
    static const s32 sGrowAccelTimeMax = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(s32, sOffsetPointNum, 3);
    NEBULA_TUNABLE(f32, sInterval, 30.0f);
    NEBULA_TUNABLE(f32, sWidthRateMin, 0.45f);
    static const f32 sDrawWidthX = 10.0f;
    static const f32 sDrawWidthZ = 10.0f;
    NEBULA_TUNABLE(f32, sTexRate, 1.0f);
    static const f32 sDrawWidthLongOffsetX = ::sDrawWidthX;
    static const f32 sDrawWidthShortOffsetX = ::sDrawWidthX;
    static const f32 sDrawWidthShortOffsetZ = ::sDrawWidthZ;

    static Color8 sColorPlusZ(0xFF, 0xFF, 0xFF, 0xFF);
    static Color8 sColorPlusX(0x64, 0x64, 0x64, 0xFF);
    static Color8 sColorMinusX(0x96, 0x96, 0x96, 0xFF);
};  // namespace

PlantStalk::PlantStalk()
    : LiveActor("茎"), mNumPlantPoints(), mPlantPoints(), mRailInfo(), mStalkLength(), mGrowthPercent(),
      mGrowSpeed(MR::getRandom(::sGrowSpeedMin, ::sGrowSpeedMax)), mGrowAccelTime(MR::getRandom(::sGrowAccelTimeMin, ::sGrowAccelTimeMax)),
      mGrownPlantPoints() {
    lumacore::setClass(this, lumacore::kClassId< PlantStalk >);
}

void PlantStalk::init(const TableIter& rIter) {
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_PlantStalk);

    initRailRider(rIter);
    mRailInfo = new PlantRailInfo(rIter, ::sInterval);

    mPosition.set(MR::getRailPos(this));
    mNumPlantPoints = mRailInfo->mNumPlantPoints;

    mPlantPoints = new PlantPoint*[mNumPlantPoints];

    MR::moveCoordToStartPos(this);

    f32 ratio = 1.0f / mNumPlantPoints;

    for (s32 idx = 0; idx < mNumPlantPoints; idx++) {
        s32 index = (mNumPlantPoints - 1) - idx;
        mPlantPoints[idx] = new PlantPoint(mRailInfo->mPlantPoints[index]->mPosition, mRailInfo->mPlantPoints[index]->mUp,
                                           MR::getEaseOutValue(idx * ratio, ::sWidthRateMin, 1.0f, 1.0f));
    }

    PlantPoint* point = mPlantPoints[0];
    point->setAxisAndPos(point->mSide, point->mUp, point->mFront, mPosition);

    MR::invalidateClipping(this);
    makeActorAppeared();
}

void PlantStalk::draw() const {
    if (MR::isValidDraw(this)) {
        TypedDraw site(MR::getSceneObj< PlantStalkDrawInit >(SceneObj_PlantStalkDrawInit)->mMaterial, nullptr, MR::getCameraViewMtx());
        drawGrowUp(site);
    }
}

bool PlantStalk::updateGrowUp() {
    mGrowAccelTime--;
    if (mGrowAccelTime <= 0) {
        mGrowSpeed += MR::getRandom(::sGrowAccelMin, ::sGrowAccelMax);
        mGrowAccelTime = MR::getRandom(::sGrowAccelTimeMin, ::sGrowAccelTimeMax);
    }

    mStalkLength += mGrowSpeed;

    if (mStalkLength <= 0.0f) {
        mStalkLength = 0.0f;
        mGrowSpeed = 0.0f;
    }

    mGrowSpeed *= ::sGrowSpeedFriction;
    mGrowSpeed = MR::clamp(mGrowSpeed, ::sGrowSpeedMin, ::sGrowSpeedMax);

    // A rail with no length is fully grown.
    mGrowthPercent = MR::getRailTotalLength(this) > 0.0f ? mStalkLength / MR::getRailTotalLength(this) : 1.0f;
    mGrowthPercent = MR::clamp(mGrowthPercent, 0.0f, 1.0f);

    if (mStalkLength >= MR::getRailTotalLength(this)) {
        mStalkLength = MR::getRailTotalLength(this);
        mGrowthPercent = 1.0f;
        mGrownPlantPoints = mNumPlantPoints;
        return true;
    }

    mGrownPlantPoints = mStalkLength / ::sInterval;
    mGrownPlantPoints = MR::clamp(mGrownPlantPoints, 2, mNumPlantPoints);

    Vec3f stalkPos;
    Vec3f axisY;
    mRailInfo->calcPosAndAxisY(&stalkPos, &axisY, mStalkLength);

    Vec3f side(1.0f, 0.0f, 0.0f);
    Vec3f up;
    MR::makeAxisUpSide(&up, &side, axisY, side);
    mPlantPoints[0]->setAxisAndPos(side, axisY, up, stalkPos);

    for (s32 idx = 1; idx < mGrownPlantPoints; idx++) {
        PlantPoint* point = mPlantPoints[idx];
        PlantPoint* other = mRailInfo->mPlantPoints[(mGrownPlantPoints - 1) - idx];
        point->setAxisAndPos(other->mSide, other->mUp, other->mFront, other->mPosition);
    }

    return false;
}

void PlantStalk::calcPosAndAxisY(Vec3f* pPos, Vec3f* pAxisY, f32 lengthAlongRail) const {
    mRailInfo->calcPosAndAxisY(pPos, pAxisY, lengthAlongRail);
}

void PlantStalk::calcNearestPos(Vec3f* pRailPos, const Vec3f& rSrcPos) const {
    mRailInfo->calcNearestPointPos(pRailPos, rSrcPos, ::sOffsetPointNum);
}

f32 PlantStalk::calcPlayerCoord() const {
    Vec3f playerPos = *MR::getPlayerCenterPos();
    f32 closestMag = cNum_calcPlayerCoord_f1;
    s32 closestPoint = 0;

    for (s32 idx = 0; idx < mGrownPlantPoints; idx++) {
        f32 mag = mPlantPoints[idx]->mPosition.squared(playerPos);

        if (mag < closestMag) {
            closestPoint = idx;
            closestMag = mag;
        }
    }

    if (mStalkLength > cNum_calcPlayerCoord_f2) {
        f32 maxLength = MR::getRailTotalLength(this);
        return MR::clamp(((static_cast< f32 >(mGrownPlantPoints - closestPoint) * ::sInterval) / mStalkLength) * MR::getRailTotalLength(this), 0.0f, maxLength);
    } else {
        return MR::getRailTotalLength(this);
    }
}

void PlantStalk::drawGrowUp(TypedDraw& rDraw) const {
    VertexPCT* vtx = rDraw.vertices< VertexPCT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, mGrownPlantPoints * 2);
    for (s32 idx = 0; idx < mGrownPlantPoints; idx++) {
        PlantPoint* point = mPlantPoints[idx];
        Vec3f side(mPlantPoints[idx]->mSide);
        Vec3f front(mPlantPoints[idx]->mFront);
        side.scale(mPlantPoints[idx]->mThickness);
        front.scale(mPlantPoints[idx]->mThickness);

        *vtx++ = {point->mPosition.x + ::sDrawWidthLongOffsetX * front.x, point->mPosition.y + ::sDrawWidthLongOffsetX * front.y, point->mPosition.z + ::sDrawWidthLongOffsetX * front.z, Rgba8(::sColorPlusZ), 0.0f, idx * ::sTexRate};

        *vtx++ = {point->mPosition.x + ::sDrawWidthShortOffsetX * side.x - ::sDrawWidthShortOffsetZ * front.x, point->mPosition.y + ::sDrawWidthShortOffsetX * side.y - ::sDrawWidthShortOffsetZ * front.y, point->mPosition.z + ::sDrawWidthShortOffsetX * side.z - ::sDrawWidthShortOffsetZ * front.z, Rgba8(::sColorPlusX), 1.0f, idx * ::sTexRate};
    }

    vtx = rDraw.vertices< VertexPCT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, mGrownPlantPoints * 2);
    for (s32 idx = 0; idx < mGrownPlantPoints; idx++) {
        PlantPoint* point = mPlantPoints[idx];
        Vec3f side(mPlantPoints[idx]->mSide);
        Vec3f front(mPlantPoints[idx]->mFront);
        side.scale(mPlantPoints[idx]->mThickness);
        front.scale(mPlantPoints[idx]->mThickness);

        *vtx++ = {point->mPosition.x - ::sDrawWidthShortOffsetX * side.x - ::sDrawWidthShortOffsetZ * front.x, point->mPosition.y - ::sDrawWidthShortOffsetX * side.y - ::sDrawWidthShortOffsetZ * front.y, point->mPosition.z - ::sDrawWidthShortOffsetX * side.z - ::sDrawWidthShortOffsetZ * front.z, Rgba8(::sColorMinusX), 0.0f, idx * ::sTexRate};

        *vtx++ = {point->mPosition.x + ::sDrawWidthLongOffsetX * front.x, point->mPosition.y + ::sDrawWidthLongOffsetX * front.y, point->mPosition.z + ::sDrawWidthLongOffsetX * front.z, Rgba8(::sColorPlusZ), 1.0f, idx * ::sTexRate};
    }

    vtx = rDraw.vertices< VertexPCT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, mGrownPlantPoints * 2);
    for (s32 idx = 0; idx < mGrownPlantPoints; idx++) {
        PlantPoint* point = mPlantPoints[idx];
        Vec3f side(mPlantPoints[idx]->mSide);
        Vec3f front(mPlantPoints[idx]->mFront);
        side.scale(mPlantPoints[idx]->mThickness);
        front.scale(mPlantPoints[idx]->mThickness);

        *vtx++ = {point->mPosition.x + ::sDrawWidthShortOffsetX * side.x - ::sDrawWidthShortOffsetZ * front.x, point->mPosition.y + ::sDrawWidthShortOffsetX * side.y - ::sDrawWidthShortOffsetZ * front.y, point->mPosition.z + ::sDrawWidthShortOffsetX * side.z - ::sDrawWidthShortOffsetZ * front.z, Rgba8(::sColorPlusX), 0.0f, idx * ::sTexRate};

        *vtx++ = {point->mPosition.x - ::sDrawWidthShortOffsetX * side.x - ::sDrawWidthShortOffsetZ * front.x, point->mPosition.y - ::sDrawWidthShortOffsetX * side.y - ::sDrawWidthShortOffsetZ * front.y, point->mPosition.z - ::sDrawWidthShortOffsetX * side.z - ::sDrawWidthShortOffsetZ * front.z, Rgba8(::sColorMinusX), 1.0f, idx * ::sTexRate};
    }
}

PlantStalkDrawInit::PlantStalkDrawInit(const char* pName) : NameObj(pName) {
    lumacore::setClass(this, lumacore::kClassId< PlantStalkDrawInit >);
    mTexture = nullptr;
    mTexture = new GameTexture(MR::loadTexFromArc("Plant", "PlantStalk.bti"));

    mMaterial = makeMaterial();
}

SiteMaterial PlantStalkDrawInit::makeMaterial() const {
    // runtime/src/render/materials/tex_modulate.hlsl: the texture times the vertex colour, its alpha the texture's; drawn where
    // the alpha is above 0, depth tested and written, back faces culled.
    SiteMaterial material;
    material.init(NEBULA_SHADER_TEX_MODULATE, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL, true, NEBULA_CULL_BACK);
    material.constants[0] = 1.0f;
    material.constants[1] = 0.0f;
    material.setTexture(0, mTexture);
    return material;
}
