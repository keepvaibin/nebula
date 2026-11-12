#include <nebula_game_api.h>
#include "Game/Util/CameraUtil.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Map/WaterPlant.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Render/Clip.hpp"
#include <nebula_geometry.hpp>
#include "Game/Render/GameTexture.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    // runtime/src/render/materials/water_plant.hlsl: the plant texture where its alpha is above 50, blended by it,
    // depth tested and written, no culling, fogged linearly toward the water's blue between `fogStart` and `fogEnd`.
    SiteMaterial plantMaterial(GameTexture* pTexture, f32 fogStart, f32 fogEnd) {
        SiteMaterial material;
        material.init(NEBULA_SHADER_WATER_PLANT, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL, true,
                           NEBULA_CULL_NONE);
        material.constants[0] = fogStart;
        material.constants[1] = fogEnd;
        material.setColor(4, 0x32, 0x32, 0x96, 0xFF);
        material.setTexture(0, pTexture);
        
        return material;
    }
}  // namespace
namespace {
    // Texture coordinates of the strip's eight vertices, top pair first.
    const f32 cTexCoords[] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.2f, 1.0f, 0.2f, 0.0f, 0.5f, 1.0f, 0.5f, 0.0f, 1.0f, 1.0f, 1.0f};
    // Base height by placement arg 3 (the texture).
    const f32 cHeightTable[] = {150.0f, 200.0f, 300.0f, 250.0f};
    const Vec3f cInitialCameraSide(1.0f, 0.0f, 0.0f);

    // Phase step per frame and amplitude of the shared sway. The ramp toward them never ran (start == target).
    const f32 cSwingSpeed = 0.03f;
    const f32 cSwingWidth = 20.0f;
    // Phase between consecutive entries of the sway table.
    const f32 cSwingPosPhaseStep = 0.2f;

    const s32 cDefaultPlantNum = 16;
    const f32 cDefaultAreaRadius = 500.0f;
    const s32 cSwingPosTableSize = 64;
    // Placement tries per plant before keeping the last random spot.
    const s32 cPlantAddTime = 10;
    const f32 cGroundProbeHeight = 500.0f;
    const f32 cGroundProbeLength = 1000.0f;
    // Plants rotate their face by this many degrees each.
    const f32 cPlantTurnDegree = 15.0f;
    const f32 cSpinWaveDistanceMax = 1500.0f;
    const s32 cSpinWaveSwingTimeMax = NebulaFrameClock::stepsOfMilliseconds(1500);
    const f32 cSpinWaveSwingTimeMaxF = 90.0f;
    const s32 cSpinWaveValidTime = 20;
    // Frames of delay per unit of distance before the spin wave reaches a plant.
    const f32 cSpinWaveDelayRate = 0.1f;
    const f32 cSpinWaveSwingRate = 400.0f;
    const f32 cDefaultHalfWidth = 20.0f;
    const f32 cHeightRate0 = 0.5f;
    const f32 cHeightRate1 = 0.8f;
    const f32 cSpinSwingRate0 = 0.3f;
    const f32 cSpinSwingRate1 = 0.6f;
    const f32 cFogStartZ = 2000.0f;
    const f32 cFogEndZ = 10000.0f;

    // Sway table offsets of a plant's low and middle joints from its tip's entry.
    const s32 cSwingIndexLow = 6;
    const s32 cSwingIndexMiddle = 3;

    inline WaterPlantDrawInit* getWaterPlantDrawInit() {
        return MR::getSceneObj< WaterPlantDrawInit >(SceneObj_WaterPlantDrawInit);
    }
}  // namespace

WaterPlant::WaterPlant(const char* pName)
    : LiveActor(pName), mPlantNum(::cDefaultPlantNum), mPlantDataArray(), mAreaRadius(::cDefaultAreaRadius), mIsSpin(),
      mHalfWidth(::cDefaultHalfWidth), mHeight(), mType() {
    lumacore::setClass(this, lumacore::kClassId< WaterPlant >);
}

void WaterPlant::init(const TableIter& rIter) {
    MR::createSceneObj(SceneObj_WaterPlantDrawInit);
    MR::connectToScene(this, MR::MovementType_MapObj, -1, -1, MR::DrawType_WaterPlant);

    MR::initDefaultPos(this, rIter);

    MR::getPlacementArg0NoInit(rIter, &mPlantNum);
    if (mPlantNum < 0) {
        mPlantNum = 0;
    }
    MR::getPlacementArg1NoInit(rIter, &mAreaRadius);
    MR::getPlacementArg3NoInit(rIter, &mType);
    if (mType < 0 || mType >= ARRAY_SIZE(::cHeightTable)) {
        mType = 0;
    }

    mHeight = ::cHeightTable[mType];
    initPlant();

    MR::setClippingTypeSphere(this, Vec2f(mAreaRadius, 2.0f * mHeight).length());

    makeActorAppeared();
}

void WaterPlant::initAfterPlacement() {
    initPlantAfterPlacement();
}

void WaterPlant::movement() {
    if (mIsSpin) {
        s32 count = 0;

        for (int i = 0; i < mPlantNum; i++) {
            PlantData& rPlant = mPlantDataArray[i];

            if (rPlant.mSpinWaveTimer >= 0) {
                count++;
                rPlant.mSpinWaveTimer--;

                if (rPlant.mSpinWaveTimer <= 0) {
                    rPlant.mSpinWaveSwingTimer = ::cSpinWaveSwingTimeMax;
                    rPlant.mSpinSwingDir.set(rPlant.mTempSpinSwingDir);
                }
            }

            if (rPlant.mSpinWaveSwingTimer >= 0) {
                count++;
                rPlant.mSpinWaveSwingTimer--;
                f32 rate = static_cast< f32 >(rPlant.mSpinWaveSwingTimer) / ::cSpinWaveSwingTimeMaxF;
                rPlant.mSpinWaveSwingVec1.set(rPlant.mSpinSwingDir);
                rPlant.mSpinWaveSwingVec2.set(rPlant.mSpinSwingDir);
                rPlant.mSpinWaveSwingVec3.set(rPlant.mSpinSwingDir);

                f32 spinSwingRate = rPlant.mSpinSwingStrength * (::cSpinWaveSwingRate * (rate * MR::sinDegree(180.0f * (1.0f - rate))));
                rPlant.mSpinWaveSwingVec1.scale(::cSpinSwingRate0 * spinSwingRate);
                rPlant.mSpinWaveSwingVec2.scale(::cSpinSwingRate1 * spinSwingRate);
                rPlant.mSpinWaveSwingVec3.scale(spinSwingRate);
            }
        }

        if (count == 0) {
            mIsSpin = false;
        }
    }

    if (!MR::isMainSpin(PLAYER_1)) {
        return;
    }

    f32 distToPlayer = MR::calcDistanceToPlayer(this);
    if (distToPlayer + mAreaRadius > ::cSpinWaveDistanceMax) {
        return;
    }

    for (int i = 0; i < mPlantNum; i++) {
        PlantData& rPlant = mPlantDataArray[i];

        if (rPlant.mSpinWaveSwingTimer > ::cSpinWaveValidTime) {
            continue;
        }

        f32 distPlayerToPlant = rPlant.mPosition.distance(*MR::getPlayerPos());

        if (distPlayerToPlant > ::cSpinWaveDistanceMax) {
            continue;
        }

        Vec3f dirPlantToPlayer(rPlant.mPosition);
        dirPlantToPlayer.sub(*MR::getPlayerPos());
        MR::normalizeOrZero(&dirPlantToPlayer);

        rPlant.mTempSpinSwingDir.set(dirPlantToPlayer);

        s32 newSpinWaveTime = static_cast< s32 >(distPlayerToPlant * ::cSpinWaveDelayRate);

        // A wave already on its way that arrives no later than this one stays.
        if (rPlant.mSpinWaveTimer > 0 && newSpinWaveTime >= rPlant.mSpinWaveTimer) {
            continue;
        }

        rPlant.mSpinWaveTimer = newSpinWaveTime;
        rPlant.mSpinSwingStrength =
            MR::getEaseInValue((::cSpinWaveDistanceMax - distPlayerToPlant) / ::cSpinWaveDistanceMax, 0.0f, 1.0f, 1.0f);
        mIsSpin = true;
    }
}

void WaterPlant::draw() const {
    if (!MR::isValidDraw(this)) {
        return;
    }

    const WaterPlantDrawInit* pDrawInit = ::getWaterPlantDrawInit();
    const SiteMaterial material = pDrawInit->material(mType);
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());
    drawDirect(site, pDrawInit->mCameraSide);
}

void WaterPlant::initPlant() {
    mPlantDataArray = new WaterPlant::PlantData[mPlantNum];
    MR::setClippingTypeSphere(this, mAreaRadius);
}

void WaterPlant::initPlantAfterPlacement() {
    f32* pSwingPosTable = ::getWaterPlantDrawInit()->mSwingPosTable;
    s32 index = 0;
    Pos3f mtx;
    mtx.identity();
    MR::makeMtxRotateY(mtx, ::cPlantTurnDegree);
    Vec3f front(0.0f, 0.0f, 1.0f);

    for (int i = 0; i < mPlantNum; i++) {
        PlantData& rPlant = mPlantDataArray[i];
        f32 randomBounds = mAreaRadius;
        Vec3f* pPlantPos = &rPlant.mPosition;
        for (int j = 0; j < ::cPlantAddTime; j++) {
            pPlantPos->set< f32 >(MR::getRandom(-randomBounds, randomBounds) + mPosition.x, ::cGroundProbeHeight + mPosition.y,
                                  MR::getRandom(-randomBounds, randomBounds) + mPosition.z);

            if (MR::calcMapGround(*pPlantPos, pPlantPos, ::cGroundProbeLength)) {
                break;
            }
        }

        rPlant.mRotation.set(front);
        rPlant.mSpinSwingDir.zero();
        rPlant.mTempSpinSwingDir.zero();
        rPlant.mSpinWaveSwingVec1.zero();
        rPlant.mSpinWaveSwingVec2.zero();
        rPlant.mSpinWaveSwingVec3.zero();
        rPlant.mSpinWaveTimer = 0;
        rPlant.mSpinWaveSwingTimer = 0;
        rPlant.mSpinSwingStrength = 0.0f;

        f32 height = MR::getRandom(mHeight, 2.0f * mHeight);
        rPlant.mHeight1 = height * ::cHeightRate0;
        rPlant.mHeight2 = height * ::cHeightRate1;
        rPlant.mHeight3 = height;

        rPlant.mSwingStrength1 = &pSwingPosTable[index + ::cSwingIndexLow];
        rPlant.mSwingStrength2 = &pSwingPosTable[index + ::cSwingIndexMiddle];
        rPlant.mSwingStrength3 = &pSwingPosTable[index];

        // Steps of 60 modulo 57 keep index + cSwingIndexLow inside the table.
        index = (index + ::cSwingPosTableSize - 4) % (::cSwingPosTableSize - ::cSwingIndexLow - 1);

        mtx.mult(front, front);
    }
}

void WaterPlant::drawDirect(TypedDraw& rDraw, const Vec3f& rSide) const {
    f32 drawOffsetX = mHalfWidth * rSide.x;
    f32 drawOffsetZ = mHalfWidth * rSide.z;

    for (int i = 0; i < mPlantNum; i++) {
        const PlantData& rPlant = mPlantDataArray[i];
        const Vec3f* position = &rPlant.mPosition;
        Vec3f drawVec1(rPlant.mRotation);
        Vec3f drawVec2(drawVec1);
        Vec3f drawVec3(drawVec1);
        drawVec1.scale(*rPlant.mSwingStrength1);
        drawVec2.scale(*rPlant.mSwingStrength2);
        drawVec3.scale(*rPlant.mSwingStrength3);

        drawVec1.add(*position);
        drawVec2.add(*position);
        drawVec3.add(*position);

        if (rPlant.mSpinWaveSwingTimer > 0) {
            drawVec1.add(rPlant.mSpinWaveSwingVec1);
            drawVec2.add(rPlant.mSpinWaveSwingVec2);
            drawVec3.add(rPlant.mSpinWaveSwingVec3);
        }

        drawVec1.y += rPlant.mHeight1;
        drawVec2.y += rPlant.mHeight2;
        drawVec3.y += rPlant.mHeight3;

        // Each vertex's texture coordinate is its entry of ::cTexCoords.
        VertexPT* vtx = rDraw.vertices< VertexPT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 8);
        *vtx++ = {drawVec3.x - drawOffsetX, drawVec3.y, drawVec3.z - drawOffsetZ, ::cTexCoords[2 * 0], ::cTexCoords[2 * 0 + 1]};
        *vtx++ = {drawVec3.x + drawOffsetX, drawVec3.y, drawVec3.z + drawOffsetZ, ::cTexCoords[2 * 1], ::cTexCoords[2 * 1 + 1]};
        *vtx++ = {drawVec2.x - drawOffsetX, drawVec2.y, drawVec2.z - drawOffsetZ, ::cTexCoords[2 * 2], ::cTexCoords[2 * 2 + 1]};
        *vtx++ = {drawVec2.x + drawOffsetX, drawVec2.y, drawVec2.z + drawOffsetZ, ::cTexCoords[2 * 3], ::cTexCoords[2 * 3 + 1]};
        *vtx++ = {drawVec1.x - drawOffsetX, drawVec1.y, drawVec1.z - drawOffsetZ, ::cTexCoords[2 * 4], ::cTexCoords[2 * 4 + 1]};
        *vtx++ = {drawVec1.x + drawOffsetX, drawVec1.y, drawVec1.z + drawOffsetZ, ::cTexCoords[2 * 5], ::cTexCoords[2 * 5 + 1]};
        *vtx++ = {position->x - drawOffsetX, position->y, position->z - drawOffsetZ, ::cTexCoords[2 * 6], ::cTexCoords[2 * 6 + 1]};
        *vtx++ = {position->x + drawOffsetX, position->y, position->z + drawOffsetZ, ::cTexCoords[2 * 7], ::cTexCoords[2 * 7 + 1]};
    }
}

WaterPlantDrawInit::WaterPlantDrawInit()
    : NameObj("水草の描画初期化"), mAngleOffset(), mSwingPosTable(), mCameraSide(::cInitialCameraSide), mPlantATex(), mPlantBTex(),
      mPlantCTex(), mPlantDTex() {
    lumacore::setClass(this, lumacore::kClassId< WaterPlantDrawInit >);
    MR::connectToScene(this, MR::MovementType_MapObj, -1, -1, -1);

    mPlantATex = new GameTexture(MR::loadTexFromArc("WaterPlant", "WaterPlantA.bti"));
    mPlantBTex = new GameTexture(MR::loadTexFromArc("WaterPlant", "WaterPlantB.bti"));
    mPlantCTex = new GameTexture(MR::loadTexFromArc("WaterPlant", "WaterPlantC.bti"));
    mPlantDTex = new GameTexture(MR::loadTexFromArc("WaterPlant", "WaterPlantD.bti"));

    mSwingPosTable = new f32[::cSwingPosTableSize];
    updateSwingPos();
}

void WaterPlantDrawInit::movement() {
    updateSwingPos();
    mAngleOffset += ::cSwingSpeed;
    if (mAngleOffset > TWO_PI) {
        mAngleOffset = 0.0f;
    }

    Pos3f inverseViewMtx;
    NebulaMtxInverse(MR::getCameraViewMtx(), inverseViewMtx);
    mCameraSide.set< f32 >(inverseViewMtx[0][0], inverseViewMtx[1][0], inverseViewMtx[2][0]);
}

SiteMaterial WaterPlantDrawInit::material(int tex) const {
    GameTexture* pTexture = tex == 1 ? mPlantBTex : tex == 2 ? mPlantCTex : tex == 3 ? mPlantDTex : mPlantATex;
    return plantMaterial(pTexture, ::cFogStartZ, ::cFogEndZ);
}

void WaterPlantDrawInit::updateSwingPos() {
    f32 angle = 0.0f;

    for (int i = 0; i < ::cSwingPosTableSize; i++) {
        mSwingPosTable[i] = ::cSwingWidth * MR::sin(mAngleOffset + angle);
        angle += ::cSwingPosPhaseStep;
    }
}
