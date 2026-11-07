#include "Game/Util/CameraUtil.hpp"
#include "Game/Map/WaterPlant.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Render/Clip.hpp"
#include <nebula_geometry.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <nebula_game_api.h>
#include <cstring>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // runtime/src/render/materials/water_plant.hlsl: the plant texture where its alpha is above 50, blended by it,
    // depth tested and written, no culling, fogged linearly toward the water's blue between `fogStart` and `fogEnd`.
    SiteMaterial plantMaterial(JUTTexture* pTexture, f32 fogStart, f32 fogEnd) {
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
    // Texture coordinates.
    f32 cTexCoords[] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.2f, 1.0f, 0.2f, 0.0f, 0.5f, 1.0f, 0.5f, 0.0f, 1.0f, 1.0f, 1.0f};
    static f32 sHeightTable[] = {150.0f, 200.0f, 300.0f, 250.0f};
    static Vec3f sDrawVec(1.0f, 0.0f, 0.0f);
    static f32 sSwingSpeed = 0.03f;
    static f32 sSwingWidth = 15.0f;

    static const s32 sDefaultPlantNum = 16;
    static const f32 sDefaultAreaRadius = 500.0f;
    static const s32 sSwingPosTableSize = 64;
    static const s32 sPlantAddTime = 10;
    static const f32 sSwingSpeedMin = 0.03f;
    static const f32 sSwingSpeedMax = 0.03f;
    static const f32 sSwingSpeedDown = 0.001f;
    static const f32 sSwingWidthMin = 20.0f;
    static const f32 sSwingWidthMax = 20.0f;
    static const f32 sSwingWidthDown = 0.5f;
    static const f32 sSpinWaveDistanceMax = 1500.0f;
    // static const s32 sSpinWaveTimeRate
    static const s32 sSpinWaveSwingTimeMax = 90;
    static const s32 sSpinWaveValidTime = 20;
    static const f32 sSpwinWaveSwingRate = 400.0f;
    // static const f32 sSwinWaveWidth
    // static const s32 sInterval
    static const f32 sDefaultHalfWidth = 20.0f;
    static const f32 sHeightRate0 = 0.5f;
    static const f32 sHeightRate1 = 0.8f;
    static const f32 sSpinSwingRate0 = 0.3f;
    static const f32 sSpinSwingRate1 = 0.6f;
    static const f32 sFogStartZ = 2000.0f;
    static const f32 sFogEndZ = 10000.0f;
};  // namespace

namespace {
    // function exists in debug map
    inline WaterPlantDrawInit* getWaterPlantDrawInit() {
        return MR::getSceneObj< WaterPlantDrawInit >(SceneObj_WaterPlantDrawInit);
    }
};  // namespace

WaterPlant::WaterPlant(const char* pName)
    : LiveActor(pName), mPlantNum(::sDefaultPlantNum), mPlantDataArray(), mAreaRadius(::sDefaultAreaRadius), mIsSpin(),
      mHalfWidth(::sDefaultHalfWidth), mHeight(), mType() {
}

void WaterPlant::init(const TableIter& rIter) {
    MR::createSceneObj(SceneObj_WaterPlantDrawInit);
    MR::connectToScene(this, MR::MovementType_MapObj, -1, -1, MR::DrawType_WaterPlant);

    MR::initDefaultPos(this, rIter);

    MR::getPlacementArg0NoInit(rIter, &mPlantNum);
    MR::getPlacementArg1NoInit(rIter, &mAreaRadius);
    MR::getPlacementArg3NoInit(rIter, &mType);

    mHeight = ::sHeightTable[mType];
    initPlant();

    MR::setClippingTypeSphere(this, Vec2f(mAreaRadius, 2.0f * mHeight).length());

    makeActorAppeared();
}

void WaterPlant::initAfterPlacement() {
    initPlantAfterPlacement();
}

// TODO remove me
void WaterPlant::movement() {
    if (mIsSpin) {
        s32 count = 0;

        for (int i = 0; i < mPlantNum; i++) {
            if (mPlantDataArray[i].mSpinWaveTimer >= 0) {
                count++;
                mPlantDataArray[i].mSpinWaveTimer--;

                if (mPlantDataArray[i].mSpinWaveTimer <= 0) {
                    mPlantDataArray[i].mSpinWaveSwingTimer = ::sSpinWaveSwingTimeMax;
                    mPlantDataArray[i].mSpinSwingDir.set(mPlantDataArray[i].mTempSpinSwingDir);
                }
            }

            if (mPlantDataArray[i].mSpinWaveSwingTimer >= 0) {
                count++;
                mPlantDataArray[i].mSpinWaveSwingTimer--;
                f32 flt = static_cast< f32 >(mPlantDataArray[i].mSpinWaveSwingTimer) / 90.0f;
                mPlantDataArray[i].mSpinWaveSwingVec1.set(mPlantDataArray[i].mSpinSwingDir);
                mPlantDataArray[i].mSpinWaveSwingVec2.set(mPlantDataArray[i].mSpinSwingDir);
                mPlantDataArray[i].mSpinWaveSwingVec3.set(mPlantDataArray[i].mSpinSwingDir);

                f32 spinSwingRate = mPlantDataArray[i].mSpinSwingStrength * (::sSpwinWaveSwingRate * (flt * MR::sinDegree(180.0f * (1.0f - flt))));
                mPlantDataArray[i].mSpinWaveSwingVec1.scale(::sSpinSwingRate0 * spinSwingRate);
                mPlantDataArray[i].mSpinWaveSwingVec2.scale(::sSpinSwingRate1 * spinSwingRate);
                mPlantDataArray[i].mSpinWaveSwingVec3.scale(spinSwingRate);
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
    if (distToPlayer + mAreaRadius > ::sSpinWaveDistanceMax) {
        return;
    }

    for (int i = 0; i < mPlantNum; i++) {
        if (mPlantDataArray[i].mSpinWaveSwingTimer > ::sSpinWaveValidTime) {
            continue;
        }

        f32 distPlayerToPlant = mPlantDataArray[i].mPosition.distance(*MR::getPlayerPos());

        if (distPlayerToPlant > ::sSpinWaveDistanceMax) {
            continue;
        }

        Vec3f dirPlantToPlayer(mPlantDataArray[i].mPosition);
        dirPlantToPlayer.sub(*MR::getPlayerPos());
        MR::normalizeOrZero(&dirPlantToPlayer);

        mPlantDataArray[i].mTempSpinSwingDir.set(dirPlantToPlayer);

        s32 newSpinWaveTime = static_cast< s32 >(distPlayerToPlant * 0.1f);

        // inline needed to get the correct comparison "optimization" to appear
        if (mPlantDataArray[i].mSpinWaveTimer > 0 && mPlantDataArray[i].newSpinWaveTimerIsGreater(newSpinWaveTime)) {
            continue;
        }

        mPlantDataArray[i].mSpinWaveTimer = newSpinWaveTime;
        mPlantDataArray[i].mSpinSwingStrength =
            MR::getEaseInValue((::sSpinWaveDistanceMax - distPlayerToPlant) / ::sSpinWaveDistanceMax, 0.0f, 1.0f, 1.0f);
        mIsSpin = true;
    }
}

void WaterPlant::draw() const {
    if (!MR::isValidDraw(this)) {
        return;
    }

    const SiteMaterial material = ::getWaterPlantDrawInit()->material(mType);
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());
    drawDirect(site);
}

void WaterPlant::initPlant() {
    mPlantDataArray = new WaterPlant::PlantData[mPlantNum];
    MR::setClippingTypeSphere(this, mAreaRadius);
}

WaterPlant::PlantData::PlantData() {
}

void WaterPlant::initPlantAfterPlacement() {
    s32 index = 0;
    Pos3f mtx;
    mtx.identity();
    MR::makeMtxRotateY(mtx, 15.0f);
    Vec3f vec(0.0f, 0.0f, 1.0f);

    int i = 0;
    if (mPlantNum > 0) {
        for (; i < mPlantNum; i++) {
            f32 randomBounds = mAreaRadius;
            Vec3f* currentPlantPos = &mPlantDataArray[i].mPosition;
            for (int j = 0; j < ::sPlantAddTime; j++) {
                currentPlantPos->set< f32 >(MR::getRandom(-randomBounds, randomBounds) + mPosition.x, 500.0f + mPosition.y,
                                            MR::getRandom(-randomBounds, randomBounds) + mPosition.z);

                if (MR::calcMapGround(*currentPlantPos, currentPlantPos, 1000.0f)) {
                    break;
                }
            }

            mPlantDataArray[i].mRotation.set(vec);
            mPlantDataArray[i].mSpinSwingDir.zero();
            mPlantDataArray[i].mTempSpinSwingDir.zero();
            mPlantDataArray[i].mSpinWaveSwingVec1.zero();
            mPlantDataArray[i].mSpinWaveSwingVec2.zero();
            mPlantDataArray[i].mSpinWaveSwingVec3.zero();
            mPlantDataArray[i].mSpinWaveTimer = 0;
            mPlantDataArray[i].mSpinWaveSwingTimer = 0;
            mPlantDataArray[i].mSpinSwingStrength = 0.0f;

            f32 random = MR::getRandom(mHeight, 2.0f * mHeight);
            mPlantDataArray[i].mHeight1 = random * ::sHeightRate0;
            mPlantDataArray[i].mHeight2 = random * ::sHeightRate1;
            mPlantDataArray[i].mHeight3 = random;

            mPlantDataArray[i].mSwingStrength1 = &::getWaterPlantDrawInit()->mSwingPosTable[index + 6];
            mPlantDataArray[i].mSwingStrength2 = &::getWaterPlantDrawInit()->mSwingPosTable[index + 3];
            mPlantDataArray[i].mSwingStrength3 = &::getWaterPlantDrawInit()->mSwingPosTable[index];

            index = (index + ::sSwingPosTableSize - 4) % (::sSwingPosTableSize - 6 - 1);

            mtx.mult(vec, vec);
        }
    }
}

void WaterPlant::drawDirect(TypedDraw& rDraw) const {
    for (int i = 0; i < mPlantNum; i++) {
        Vec3f* position(&mPlantDataArray[i].mPosition);
        Vec3f drawVec1(mPlantDataArray[i].mRotation);
        Vec3f drawVec2(drawVec1);
        Vec3f drawVec3(drawVec1);
        drawVec1.scale(*mPlantDataArray[i].mSwingStrength1);
        drawVec2.scale(*mPlantDataArray[i].mSwingStrength2);
        drawVec3.scale(*mPlantDataArray[i].mSwingStrength3);

        drawVec1.add(*position);
        drawVec2.add(*position);
        drawVec3.add(*position);

        if (mPlantDataArray[i].mSpinWaveSwingTimer > 0) {
            drawVec1.add(mPlantDataArray[i].mSpinWaveSwingVec1);
            drawVec2.add(mPlantDataArray[i].mSpinWaveSwingVec2);
            drawVec3.add(mPlantDataArray[i].mSpinWaveSwingVec3);
        }

        drawVec1.y += mPlantDataArray[i].mHeight1;
        drawVec2.y += mPlantDataArray[i].mHeight2;
        drawVec3.y += mPlantDataArray[i].mHeight3;

        f32 drawOffsetX = mHalfWidth * ::sDrawVec.x;
        f32 drawOffsetZ = mHalfWidth * ::sDrawVec.z;
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
    : NameObj("水草の描画初期化"), mAngleOffset(), mSwingPosTable(), mPlantATex(), mPlantBTex(), mPlantCTex(), mPlantDTex() {
    MR::connectToScene(this, MR::MovementType_MapObj, -1, -1, -1);

    mPlantATex = new JUTTexture(MR::loadTexFromArc("WaterPlant.arc", "WaterPlantA.bti"));
    mPlantBTex = new JUTTexture(MR::loadTexFromArc("WaterPlant.arc", "WaterPlantB.bti"));
    mPlantCTex = new JUTTexture(MR::loadTexFromArc("WaterPlant.arc", "WaterPlantC.bti"));
    mPlantDTex = new JUTTexture(MR::loadTexFromArc("WaterPlant.arc", "WaterPlantD.bti"));

    mSwingPosTable = new f32[::sSwingPosTableSize];
    updateSwingPos();
}

void WaterPlantDrawInit::movement() {
    updateSwingPos();
    mAngleOffset += ::sSwingSpeed;
    if (mAngleOffset > TWO_PI) {
        mAngleOffset = 0.0f;
    }

    if (::sSwingSpeed > ::sSwingSpeedMax) {
        ::sSwingSpeed -= ::sSwingSpeedDown;
        if (::sSwingSpeed < ::sSwingSpeedMin) {
            ::sSwingSpeed = ::sSwingSpeedMin;
        }
    } else {
        ::sSwingSpeed = ::sSwingSpeedMax;
    }

    if (::sSwingWidth > ::sSwingWidthMax) {
        ::sSwingWidth -= ::sSwingWidthDown;
        if (::sSwingWidth < ::sSwingWidthMin) {
            ::sSwingWidth = ::sSwingWidthMin;
        }
    } else {
        ::sSwingWidth = ::sSwingWidthMax;
    }

    Pos3f inverseViewMtx;
    NebulaMtxInverse(MR::getCameraViewMtx(), inverseViewMtx);
    // normal getXDir call here reverses float load order
    ::sDrawVec.set< f32 >(inverseViewMtx[0][0], inverseViewMtx[1][0], inverseViewMtx[2][0]);
}

SiteMaterial WaterPlantDrawInit::material(int tex) const {
    JUTTexture* pTexture = tex == 1 ? mPlantBTex : tex == 2 ? mPlantCTex : tex == 3 ? mPlantDTex : mPlantATex;
    return plantMaterial(pTexture, ::sFogStartZ, ::sFogEndZ);
}

void WaterPlantDrawInit::updateSwingPos() {
    f32 angle = 0.0f;

    for (int i = 0; i < ::sSwingPosTableSize; i++) {
        mSwingPosTable[i] = ::sSwingWidth * MR::sin(mAngleOffset + angle);
        angle += 0.2f;
    }
}
