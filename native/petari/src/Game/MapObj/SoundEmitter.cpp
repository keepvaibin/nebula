#include "Game/MapObj/SoundEmitter.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/RailRider.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Sound/Sound.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const u32 sSeTable[33] = {0x60000, 0x6000e, 0x6000f, 0x60010, 0x60011, 0x60014, 0x501a6, 0x501a7, 0x60015, 0x60016, 0x60017,
                              0x60018, 0x60019, 0x5021b, 0x60020, 0x60021, 0x60022, 0x50272, 0x50273, 0x6002a, 0xa00a8, 0xa00a9,
                              0x6002d, 0x6002e, 0x50318, 0x6002f, 0x6001c, 0x50332, 0x60030, 0x60004, 0x17,    0x60035, 0x50363};
};  // namespace

namespace NrvSoundEmitter {
    NEW_NERVE(SoundEmitterNrvStandBy, SoundEmitter, StandBy);
    NEW_NERVE(SoundEmitterNrvPlaySound, SoundEmitter, PlaySound);
};  // namespace NrvSoundEmitter

SoundEmitter::SoundEmitter(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< SoundEmitter >);
    mSoundType = -1;
}

void SoundEmitter::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::getPlacementArg0NoInit(rIter, &mSoundType);
    if (mSoundType >= ARRAY_SIZE(::sSeTable)) {
        mSoundType = -1;  // past the table: silent, as an unset one
    }
    MR::connectToSceneMapObjMovement(this);
    MR::registerDemoSimpleCastAll(this);

    if (MR::isConnectedWithRail(rIter)) {
        initRailRider(rIter);
    }

    initSound(8, false);
    MR::setClippingTypeSphere(this, 20000.0f);
    MR::useStageSwitchReadAppear(this, rIter);
    MR::useStageSwitchReadA(this, rIter);

    if (MR::isValidSwitchAppear(this)) {
        initNerve(GET_NERVE(SoundEmitter, SoundEmitterNrvStandBy));
    } else {
        initNerve(GET_NERVE(SoundEmitter, SoundEmitterNrvPlaySound));
    }

    makeActorAppeared();
}

void SoundEmitter::control() {
    if (MR::isValidSwitchA(this)) {
        if (MR::isOnSwitchA(this)) {
            kill();
        }
    }
}

void SoundEmitter::exePlaySound() {
    RailRider* rider = mRailRider;
    s32 lifeTime = -1;

    if (rider != nullptr) {
        switch (mSoundType) {
        case 0xd:
        case 0x11:
        case 0x12: {
            rider->moveToNearestPos(MR::getCameraWatchPos());
        } break;
        default: {
            rider->moveToNearestPos(MR::getCamPos());
        } break;
        }

        mPosition.x = mRailRider->mCurPos.x;
        mPosition.y = mRailRider->mCurPos.y;
        mPosition.z = mRailRider->mCurPos.z;
    }

    switch (mSoundType) {
    case 9:
    case 0xA:
    case 0xB: {
        if (MR::calcDistanceToPlayer(this) < 1000.0f) {
            return;
        }
    } break;
    case 0x16:
    case 0x17: {
        if (MR::isDemoActive("ロゼッタ状況説明デモ")) {
            return;
        }
    } break;
    case 0x1A: {
        lifeTime = 5;
    } break;
    case 0x1E: {
        MR::startSystemSE("SE_SY_READ_RIDDLE_S");
        kill();
        return;
    } break;
    }

    if (mSoundType >= 0) {
        MR::startLevelSound(this, ::sSeTable[mSoundType], -1, -1, lifeTime);
    }
}

inline void SoundEmitter::exeStandBy() {
    if (MR::isValidSwitchAppear(this)) {
        if (MR::isOnSwitchAppear(this)) {
            setNerve(GET_NERVE(SoundEmitter, SoundEmitterNrvPlaySound));
        }
    }
}
