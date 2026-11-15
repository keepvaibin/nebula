#include "Game/MapObj/AssemblyBlock.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapPartsUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    static const s32 sDefaultTimer = NebulaFrameClock::stepsOfSeconds(5);
    const s32 sStepForAssemble = 10;
    const s32 sStepForReturn = 10;
    const s32 sFlashTime = 100;
    const f32 sReturnOffset = 50.0f;
    const f32 sFloatRotSpeedNoSign = 0.1f;
    static const char* const sReturnPosName = "合体ブロック故郷点";
};  // namespace

namespace NrvAssemblyBlock {
    NEW_NERVE(AssemblyBlockNrvWait, AssemblyBlock, Wait);
    NEW_NERVE(AssemblyBlockNrvAssemble, AssemblyBlock, Assemble);
    NEW_NERVE(AssemblyBlockNrvAssembleWait, AssemblyBlock, AssembleWait);
    NEW_NERVE(AssemblyBlockNrvReturn, AssemblyBlock, Return);
    NEW_NERVE(AssemblyBlockNrvTimer, AssemblyBlock, Timer);
};  // namespace NrvAssemblyBlock

AssemblyBlock::AssemblyBlock(const char* pName)
    : LiveActor(pName), mObjArg7(-1), mPlayerSearchDistance(-1.0f), mClipCenter(0.0f, 0.0f, 0.0f), mFloatRotateSpeed(), mTimer(::sDefaultTimer),
      mBloomModel(), mIsTimerType() {
    lumacore::setClass(this, lumacore::kClassId< AssemblyBlock >);
    mCurrentMtx.identity();
    mAssembledMtx.identity();
    mReturnMtx.identity();
}

void AssemblyBlock::init(const TableIter& rIter) {
    Vec3f defaultReturnPos;
    Vec3f assembledPos;
    Vec3f returnPos;
    Vec3f defaultReturnOffset;
    MR::FormattedName name;

    MR::initDefaultPos(this, rIter);
    MR::tryRegisterNamePosLinkObj(this, rIter);

    if (!MR::tryFindLinkNamePos(this, ::sReturnPosName, mReturnMtx.toMtxPtr())) {
        defaultReturnOffset.x = 0.0f;
        defaultReturnOffset.y = 1000.0f;
        defaultReturnOffset.z = 1000.0f;
        defaultReturnPos.add(mPosition, defaultReturnOffset);
        mReturnMtx.setTrans(defaultReturnPos);
    }

    MR::getMapPartsObjectNameIfExistShapeID(name, rIter);
    initModelManagerWithAnm(name.c_str(), nullptr, false);

    if (MR::isEqualString(name.c_str(), "AssemblyBlockPartsTimerA")) {
        mIsTimerType = true;
    } else {
        mIsTimerType = false;
    }

    mAssembledMtx.set(getBaseMtx());
    mCurrentMtx.set(mReturnMtx);
    MR::setBaseTRMtx(this, mCurrentMtx);

    if (MR::isEqualSubString(name.c_str(), "PartsIce")) {
        MR::connectToSceneIndirectMapObj(this);
    } else {
        MR::connectToSceneMapObj(this);
    }

    initHitSensor(1);
    MR::addBodyMessageSensorMapObj(this);
    MR::initCollisionPartsAutoEqualScaleOne(this, name.c_str(), getSensor(nullptr), nullptr);

    mAssembledMtx.getTrans(assembledPos);
    mReturnMtx.getTrans(returnPos);
    mClipCenter.add(assembledPos, returnPos);
    mClipCenter.x *= 0.5f;
    mClipCenter.y *= 0.5f;
    mClipCenter.z *= 0.5f;
    f32 boundingRadius;
    MR::calcModelBoundingRadius(&boundingRadius, this);
    MR::setClippingTypeSphere(this, boundingRadius + mClipCenter.distance(returnPos), &mClipCenter);
    MR::getPlacementArg0NoInit(rIter, &mPlayerSearchDistance);

    if (mPlayerSearchDistance <= 0.0f) {
        mPlayerSearchDistance = boundingRadius * 2.0f;
    }

    MR::getPlacementArg1NoInit(rIter, &mTimer);
    MR::getPlacementArg7NoInit(rIter, &mObjArg7);

    if (MR::getRandom(0, 2)) {
        mFloatRotateSpeed = ::sFloatRotSpeedNoSign;
    } else {
        mFloatRotateSpeed = -::sFloatRotSpeedNoSign;
    }

    if (mObjArg7 == -1) {
        initEffectKeeper(0, "AssemblyBlock");
    } else {
        initEffectKeeper(0, nullptr);
    }

    initSound(4, false);
    MR::initStarPointerTarget(this, boundingRadius, Vec3f(0.0f, 0.0f, 0.0f));

    if (MR::isEqualString(name.c_str(), "AssemblyBlockPartsTimerA")) {
        mBloomModel = MR::createBloomModel(this, getBaseMtx());
    }

    MR::tryStartAllAnim(this, "Wait");
    initNerve(GET_NERVE(AssemblyBlock, AssemblyBlockNrvWait));
    makeActorAppeared();
}

void AssemblyBlock::exeWait() {
    if (MR::isFirstStep(this)) {
        if (MR::isEffectValid(this, "Blur")) {
            MR::deleteEffect(this, "Blur");
        }

        MR::validateCollisionParts(this);
        MR::validateHitSensors(this);
    }

    MR::rotateMtxLocalXDegree(mCurrentMtx.toMtxPtr(), mFloatRotateSpeed);
    MR::rotateMtxLocalYDegree(mCurrentMtx.toMtxPtr(), mFloatRotateSpeed);
    MR::rotateMtxLocalZDegree(mCurrentMtx.toMtxPtr(), mFloatRotateSpeed);

    f32 scalar = MR::sinDegree(getNerveStep());

    Vec3f trans;
    mCurrentMtx.getTrans(trans);
    trans.scaleAdd(scalar, mGravity, trans);
    mCurrentMtx.setTrans(trans);

    tryStartAssemble();
}

void AssemblyBlock::exeAssemble() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "Blur");

        if (mIsTimerType) {
            MR::startSound(this, "SE_OJ_ASSEMBLE_BLOCK_ST_F");
        } else {
            MR::startSound(this, "SE_OJ_ASSEMBLE_BLOCK_START");
        }

        MR::invalidateCollisionParts(this);
        MR::invalidateHitSensors(this);
    }

    MR::blendMtx(mReturnMtx.toMtxPtr(), mAssembledMtx.toMtxPtr(), MR::calcNerveRate(this, ::sStepForAssemble), mCurrentMtx.toMtxPtr());

    if (MR::isStep(this, ::sStepForAssemble)) {
        if (mIsTimerType) {
            MR::startSound(this, "SE_OJ_ASSEMBLE_BLOCK_ED_F");
        } else {
            MR::startSound(this, "SE_OJ_ASSEMBLE_BLOCK_END");
        }

        setNerve(GET_NERVE(AssemblyBlock, AssemblyBlockNrvAssembleWait));
    }
}

void AssemblyBlock::exeAssembleWait() {
    if (MR::isFirstStep(this)) {
        if (MR::isEffectValid(this, "Blur")) {
            MR::deleteEffect(this, "Blur");
        }

        MR::tryRumblePadWeak(this, PLAYER_1);
        MR::validateCollisionParts(this);
        MR::validateHitSensors(this);
    }

    if (mObjArg7 == 0) {
        setNerve(GET_NERVE(AssemblyBlock, AssemblyBlockNrvTimer));
    } else {
        tryStartReturn();
    }
}

void AssemblyBlock::exeReturn() {
    if (MR::isFirstStep(this)) {
        MR::tryRumblePadWeak(this, PLAYER_1);
        MR::emitEffect(this, "Blur");

        if (mIsTimerType) {
            MR::startSound(this, "SE_OJ_ASSEMBLE_BLOCK_RET_F");
        } else {
            MR::startSound(this, "SE_OJ_ASSEMBLE_BLOCK_RETURN");
        }

        MR::invalidateCollisionParts(this);
        MR::invalidateHitSensors(this);
    }

    MR::blendMtx(mAssembledMtx.toMtxPtr(), mReturnMtx.toMtxPtr(), MR::calcNerveRate(this, ::sStepForReturn), mCurrentMtx.toMtxPtr());

    if (MR::isStep(this, ::sStepForReturn)) {
        setNerve(GET_NERVE(AssemblyBlock, AssemblyBlockNrvWait));
    }
}

void AssemblyBlock::exeTimer() {
    if (MR::isStep(this, mTimer - ::sFlashTime)) {
        MR::tryStartAllAnim(this, "Disappear");
    }

    if (MR::isStep(this, mTimer)) {
        if (mIsTimerType) {
            MR::startSound(this, "SE_OJ_ASSEMBLE_BLOCK_VAN_F");
        } else {
            MR::startSound(this, "SE_OJ_ASSEMBLE_BLOCK_VANISH");
        }

        if (mBloomModel) {
            mBloomModel->kill();
        }

        kill();
    }
}

void AssemblyBlock::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mCurrentMtx);
}

bool AssemblyBlock::tryStartAssemble() {
    bool isNotNear = !MR::isNearPlayerAnyTime(this, mPlayerSearchDistance);

    if (isNotNear) {
        return false;
    }

    if (MR::isPlayerDead()) {
        return false;
    }

    setNerve(GET_NERVE(AssemblyBlock, AssemblyBlockNrvAssemble));

    return true;
}

bool AssemblyBlock::tryStartReturn() {
    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        return false;
    }

    if (MR::isNearPlayerAnyTime(this, mPlayerSearchDistance + ::sReturnOffset)) {
        return false;
    }

    if (MR::isPlayerDead()) {
        return false;
    }

    setNerve(GET_NERVE(AssemblyBlock, AssemblyBlockNrvReturn));

    return true;
}
