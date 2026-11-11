#include "Game/MapObj/ElectricRailHolder.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Render/Model.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/MapObj/ElectricRailMoving.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEBULA_TUNABLE(f32, cHitSensorRadius, 30.0f);
    // Draw numbers a rail owns among the holder's: its place in the list times this. Room for the strips of a rail
    // (two planes a level) or its sent mesh.
    NEBULA_TUNABLE(u32, cPartsPerRail, 1024);

    ElectricRailHolder* getHolder() {
        return MR::getSceneObj< ElectricRailHolder >(SceneObj_ElectricRailHolder);
    }

    bool isNearRail(const LiveActor* pActor, const HitSensor* pSensor, Vec3f* pRailPos, Vec3f* pRailDir) {
        Vec3f railPos;
        f32 coord = MR::calcNearestRailPos(&railPos, pActor, pSensor->mPosition);

        const f32 sensorRadius = pSensor->mRadius;
        f32 radius = ::cHitSensorRadius + sensorRadius;

        if (railPos.squared(pSensor->mPosition) < radius * radius) {
            if (pRailPos != nullptr) {
                pRailPos->set(railPos);
            }

            if (pRailDir != nullptr) {
                MR::calcRailDirectionAtCoord(pRailDir, pActor, coord);
            }

            return true;
        }

        return false;
    }
};  // namespace

ElectricRailHolder::ElectricRailHolder(const char* pName) : LiveActor(pName), mIsActive() {
    lumacore::setClass(this, lumacore::kClassId< ElectricRailHolder >);
    MR::zeroMemory(&mRailModels, sizeof(mRailModels));
    MR::zeroMemory(&mRailItems, sizeof(mRailItems.mArray));
}

void ElectricRailHolder::calcAnim() {
    if (!mIsActive) {
        return;
    }

    for (s32 i = 0; i < mRailModels.size(); i++) {
        if (mRailModels[i] != nullptr) {
            mRailModels[i]->calcAnim();
        }
    }
}

void ElectricRailHolder::draw() const {
    if (!mIsActive) {
        return;
    }

    for (s32 i = 0; i < mRailItems.size(); i++) {
        RailItem* pItem = mRailItems[i];

        if (!MR::isValidDraw(pItem->mHost)) {
            continue;
        }

        // A rail skipped (clipped, hidden) does not renumber the rails after it.
        Frame::setPart(static_cast< u32 >(i) * ::cPartsPerRail);
        MaterialEntry* pPacket = pItem->mMatPacket;
        MaterialDraw material(pPacket->getMaterial());
        material.sendOwn(pPacket->getFirstMeshEntry()->mMaterial, pPacket->getMaterial());

        pItem->mHost->drawWith(material);
    }
}

bool ElectricRailHolder::isTouchRail(const HitSensor* pSensor, Vec3f* pVec1, Vec3f* pVec2) const {
    if (!mIsActive) {
        return false;
    }

    for (s32 i = 0; i < mRailItems.size(); i++) {
        RailItem* pItem = mRailItems[i];

        if (!MR::isValidDraw(pItem->mHost)) {
            continue;
        }

        if (pItem->mRailType == ElectricRail_RedMoving || pItem->mRailType == ElectricRail_YellowMoving) {
            if (static_cast< const ElectricRailMoving* >(pItem->mHost)->isTouchRail(pSensor, pVec1, pVec2)) {
                return true;
            }
        } else {
            if (::isNearRail(pItem->mHost, pSensor, pVec1, pVec2)) {
                return true;
            }
        }
    }

    return false;
}

void ElectricRailHolder::control() {
    mIsActive = false;

    for (s32 i = 0; i < mRailItems.size(); i++) {
        if (MR::isValidDraw(mRailItems[i]->mHost)) {
            mIsActive = true;
            break;
        }
    }

    if (!mIsActive) {
        return;
    }

    for (s32 i = 0; i < mRailModels.size(); i++) {
        if (mRailModels[i] != nullptr) {
            mRailModels[i]->movement();
        }
    }
}


void ElectricRailFunction::registerRail(const LiveActor* pActor, ElectricRailType railType) {
    MR::createSceneObj(SceneObj_ElectricRailHolder);
    ::getHolder()->registerRail(pActor, railType);
}

bool ElectricRailFunction::isTouchRail(const HitSensor* pSensor, Vec3f* pVec1, Vec3f* pVec2) {
    if (!MR::isExistSceneObj(SceneObj_ElectricRailHolder)) {
        return false;
    }

    return ::getHolder()->isTouchRail(pSensor, pVec1, pVec2);
}

f32 ElectricRailFunction::getHitSensorRadius() {
    return ::cHitSensorRadius;
}

void ElectricRailHolder::init(const TableIter& /*rIter*/) {
    MR::connectToScene(this, MR::MovementType_MapObjDecoration, MR::CalcAnimType_MapObj, MR::DrawBufferType_None, MR::DrawType_ElectricRailHolder);

    MR::invalidateClipping(this);

    MR::registerDemoSimpleCastAll(this);

    MR::createAdaptorAndConnectToDrawBloomModel("電撃レールブルーム描画", MR::Functor(this, &ElectricRailHolder::draw));

    makeActorAppeared();
}

void ElectricRailHolder::registerRail(const LiveActor* pActor, ElectricRailType railType) {
    if (mRailModels[railType] == nullptr) {
        mRailModels[railType] = createModel(railType);
    }

    RailItem* pItem = new RailItem();
    pItem->mRailType = railType;
    pItem->mHost = static_cast< const ElectricRailDrawn* >(pActor);

    Model* pModel = MR::getModel(mRailModels[railType]);
    MaterialEntry* pPacket = pModel->getMaterialEntry(MR::getMaterialNo(pModel, "SparkMat"));
    pItem->mMatPacket = pPacket;

    // push_back moves the list to a larger store past its 64 inline places (a stage with more electric rails).
    mRailItems.push_back(pItem);
}

namespace {
    // By ElectricRailType: the model, and its colour frame (0 yellow, 1 red).
    struct RailModelInfo {
        const char* mName;
        const char* mModelName;
        f32 mColorFrame;
    };

    const RailModelInfo cRailModelInfos[] = {
        {"電撃レールモデル(黄)", "ElectricRail", 0.0f},
        {"電撃レールモデル(赤)", "ElectricRail", 1.0f},
        {"移動電撃レールモデル(黄)", "ElectricRailMoving", 0.0f},
        {"移動電撃レールモデル(赤)", "ElectricRailMoving", 1.0f},
    };
}  // namespace

ModelObj* ElectricRailHolder::createModel(ElectricRailType railType) const {
    const RailModelInfo& rInfo = cRailModelInfos[railType];
    ModelObj* pModel =
        new ModelObj(rInfo.mName, rInfo.mModelName, nullptr, MR::DrawBufferType_None, MR::MovementType_None, MR::CalcAnimType_None, false);
    pModel->initWithoutIter();

    MR::startBtk(pModel, rInfo.mModelName);
    MR::startBrk(pModel, rInfo.mModelName);
    MR::setBrkFrameAndStop(pModel, rInfo.mColorFrame);
    return pModel;
}
