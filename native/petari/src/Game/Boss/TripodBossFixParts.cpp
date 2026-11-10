#include "Game/Boss/TripodBossFixParts.hpp"
#include "Game/Boss/TripodBossAccesser.hpp"
#include "Game/Boss/TripodBossFixPartsBase.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapPartsUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 250.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 3750.0f);

namespace NrvTripodBossFixParts {
    NEW_NERVE(TripodBossFixPartsNrvNonActive, TripodBossFixParts, NonActive);
    NEW_NERVE(TripodBossFixPartsNrvStartDemo, TripodBossFixParts, StartDemo);
    NEW_NERVE(TripodBossFixPartsNrvWait, TripodBossFixParts, Wait);
    NEW_NERVE(TripodBossFixPartsNrvBreak, TripodBossFixParts, Break);
};  // namespace NrvTripodBossFixParts

struct TripodBossSetting {
    const char* mPartName;
    s32 mType;
};

namespace {
    const TripodBossSetting sTripodBossSettingTable[] = {{"TripodBossBodyABloom", 3},
                                                          {"TripodBossFootBloom", 3},
                                                          {"TriPodBossLeg2ABloom", 3},
                                                          {"TripodBossEyeLight", 2},
                                                          {"TripodBossEyeLightBloom", 3}};

    const TripodBossSetting* getSetting(const char* pObjName) {
        for (u32 i = 0; i < ARRAY_SIZE(::sTripodBossSettingTable); i++) {
            const char* partName = ::sTripodBossSettingTable[i].mPartName;
            if (MR::isEqualString(pObjName, partName)) {
                return &::sTripodBossSettingTable[i];
            }
        }

        return nullptr;
    }
};  // namespace

TripodBossFixParts::TripodBossFixParts(const char* pName)
    : TripodBossFixPartsBase(pName), mBreakType(-1), mBreakKillStep(180), mHasCollision(), mHasBreakMovement(true), mIsMarioOn(), mIsPlayingWarning() {
}

void TripodBossFixParts::init(const TableIter& rIter) {
    const char* objName;
    MR::getObjectName(&objName, rIter);
    setName(objName);
    TripodBossFixPartsBase::init(rIter);
    // Setting type bit 0: drawn as bloom; bit 1: no break movement.
    bool isBloom = false;
    const TripodBossSetting* setting = ::getSetting(objName);

    if (setting != nullptr) {
        isBloom = (setting->mType & 0x1);
        mHasBreakMovement = !(setting->mType & 0x2);
    }

    MR::FormattedName mapPartsID;
    MR::getMapPartsObjectNameIfExistShapeID(mapPartsID, rIter);
    initModelManagerWithAnm(mapPartsID.c_str(), nullptr, false);

    if (isBloom) {
        MR::connectToScene(this, MR::MovementType_MapObjDecoration, MR::CalcAnimType_MapObjDecoration, MR::DrawBufferType_BloomModel,
                           MR::DrawType_None);
    } else {
        MR::connectToScene(this, MR::MovementType_MapObjDecoration, MR::CalcAnimType_MapObjDecoration, MR::DrawBufferType_TripodBoss,
                           MR::DrawType_None);
    }

    if (MR::isEqualString(objName, "TriPodBossBodyA")) {
        setClippingSphere(Vec3f(cNum_init_f1, cNum_init_f2, 0.0f), cNum_init_f3);
    } else {
        initClippingSphere();
    }

    MR::getPlacementArg1NoInit(rIter, &mBreakType);
    MR::getPlacementArg2NoInit(rIter, &mBreakKillStep);

    if (mHasBreakMovement) {
        initBreakMovement(mBreakType);
    }

    mHasCollision = MR::isExistKcl(this, mapPartsID.c_str());

    if (mHasCollision) {
        initSensor(rIter);
        MR::initCollisionParts(this, mapPartsID.c_str(), getSensor("body"), nullptr);
    }

    initEffectKeeper(0, "TripodBoss");
    initSound(5, false);
    initNerve(GET_NERVE(TripodBossFixParts, TripodBossFixPartsNrvNonActive));

    if (mHasCollision) {
        MR::invalidateCollisionParts(this);
    }

    makeActorDead();
}

void TripodBossFixParts::initSensor(const TableIter& rIter) {
    initHitSensor(1);

    if (MR::isMapPartsPressOn(rIter)) {
        MR::addBodyMessageSensorMapObjPress(this);
    } else {
        MR::addBodyMessageSensorMapObj(this);
    }
}

void TripodBossFixParts::kill() {
    switch (mBreakType) {
    case 0:
        MR::emitEffect(this, "ExplosionS");
        MR::startSound(this, "SE_BM_TRIPOD_KILL_S");
        break;
    case 1:
        MR::emitEffect(this, "ExplosionM");
        MR::startSound(this, "SE_BM_TRIPOD_KILL_M");
        MR::tryRumblePadVeryWeak(this, PLAYER_1);
        break;
    case 2:
        MR::emitEffect(this, "ExplosionM");
        MR::startSound(this, "SE_BM_TRIPOD_KILL_M");
        MR::shakeCameraVeryWeak();
        MR::tryRumblePadMiddle(this, PLAYER_1);
        break;
    case 3:
        MR::emitEffect(this, "ExplosionL");
        MR::startSound(this, "SE_BM_TRIPOD_KILL_L");
        MR::shakeCameraStrong();
        MR::tryRumblePadVeryStrong(this, PLAYER_1);
        break;
    }

    LiveActor::kill();
}

void TripodBossFixParts::activateTripodBoss() {
    if (isNerve(GET_NERVE(TripodBossFixParts, TripodBossFixPartsNrvNonActive))) {
        MR::onCalcAnim(this);

        if (mHasCollision) {
            MR::validateCollisionParts(this);
        }

        mIsActive = 1;

        if (MR::isExistAnim(this, "StartDemo")) {
            setNerve(GET_NERVE(TripodBossFixParts, TripodBossFixPartsNrvStartDemo));
        } else {
            setNerve(GET_NERVE(TripodBossFixParts, TripodBossFixPartsNrvWait));
        }
    }
}

void TripodBossFixParts::exeNonActive() {
}

void TripodBossFixParts::exeStartDemo() {
    if (MR::isFirstStep(this)) {
        MR::tryStartAllAnim(this, "StartDemo");
    }

    updateTripodMatrix();

    if (!MR::isStartDemoTripodBoss()) {
        setNerve(GET_NERVE(TripodBossFixParts, TripodBossFixPartsNrvWait));
    }
}

void TripodBossFixParts::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::tryStartAllAnim(this, "Wait");
        mIsMarioOn = false;
    }

    exeWaitOwn();
    updateTripodMatrix();

    if (MR::isRideMarioTripodBoss() && MR::isOnGroundPlayer() && !mIsMarioOn) {
        MR::tryStartAllAnim(this, "Warning");
        mIsMarioOn = true;

        if (MR::isExistAnim(this, "Warning") && !mIsPlayingWarning) {
            MR::startSound(this, "SE_BM_TRIPOD_MARKER");
            mIsPlayingWarning = true;
        }
    }

    if (MR::isLeaveMarioNowTripodBoss() && mIsMarioOn) {
        MR::tryStartAllAnim(this, "Wait");

        if (mIsPlayingWarning) {
            MR::stopSound(this, "SE_BM_TRIPOD_MARKER", 60);
        }

        mIsPlayingWarning = false;
        mIsMarioOn = false;
    }

    if (MR::isEndBreakDownDemoTripodBoss()) {
        if (mHasBreakMovement) {
            setNerve(GET_NERVE(TripodBossFixParts, TripodBossFixPartsNrvBreak));
        } else {
            kill();
        }
    }
}

void TripodBossFixParts::exeBreak() {
    if (MR::isFirstStep(this)) {
        startBreakMovement();

        if (mHasCollision) {
            MR::invalidateCollisionParts(this);
        }
    }

    updateBreakMovementMatrix();

    if (MR::isGreaterStep(this, mBreakKillStep)) {
        kill();
    }
}
