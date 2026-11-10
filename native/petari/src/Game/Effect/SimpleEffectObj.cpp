#include "Game/Effect/SimpleEffectObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvSimpleEffectObj {
    NEW_NERVE(HostTypeWait, SimpleEffectObj, Wait);
    NEW_NERVE(HostTypeMove, SimpleEffectObj, Move);
};  // namespace NrvSimpleEffectObj

namespace {
    const SimpleEffectData sEffectData[] = {
        {"IceLayerBreak", "SE_OJ_ICE_LAYER_BREAK", nullptr, 0, "最強", 7},
        {"LavaVolcanoEruption", nullptr, nullptr, 0, "最強", 0},
        {"LavaHomeVolcanoFallingRock", nullptr, "SE_AT_LV_FALLING_ROCK", -1, nullptr, 0},
        {"WaterLayerBreak", "SE_OJ_WATER_LAYER_BREAK", nullptr, 0, "最強", 7},
        {"DrainPipeBubbleA", nullptr, "SE_OJ_LV_PIPE_BUBBLE_A", -1, nullptr, 0},
        {"DrainPipeBubbleB", nullptr, "SE_OJ_LV_PIPE_BUBBLE_B", -1, nullptr, 0},
        {"StarPieceClusterBreak", "SE_OJ_CRYSTAL_CAGE_L_BREAK", nullptr, 0, "最強", 7},
        {"BattleShipExplosionMetal", "SE_AT_BTLSHIP_EXPLODE_METAL", nullptr, -1, nullptr, 0},
        {"BattleShipExplosionRock", "SE_AT_BTLSHIP_EXPLODE_ROCK", nullptr, -1, nullptr, 0},
        {"Steam", nullptr, "SE_OJ_LV_HD_STEAM", -1, nullptr, 0},
        {"IcicleRockLight", nullptr, "SE_OJ_LV_ICICLE_ROCK_LIGHT", -1, nullptr, 0},
        {"UFOKinokoLandingBlackSmoke", nullptr, "SE_OJ_LV_UFO_KINOKO_SMOKE", -1, nullptr, 0},
    };

    const SimpleEffectData* findData(const char* pName) {
        for (u32 i = 0; i < ARRAY_SIZE(sEffectData); i++) {
            if (MR::isEqualString(pName, sEffectData[i].mName)) {
                return &sEffectData[i];
            }
        }

        return nullptr;
    }
};  // namespace

SimpleEffectObj::SimpleEffectObj(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< SimpleEffectObj >);
    mObjName = nullptr;
    mData = nullptr;
}

void SimpleEffectObj::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::connectToSceneMapObjMovement(this);
    MR::getObjectName(&mObjName, rIter);
    mData = ::findData(mObjName);
    initEffectKeeper(0, mObjName);
    initSound(4, false);
    initNerve(GET_NERVE(SimpleEffectObj, HostTypeWait));

    if (MR::useStageSwitchReadA(this, rIter)) {
        MR::listenStageSwitchOnOffA(this, MR::Functor(this, &SimpleEffectObj::setStateMove), MR::Functor(this, &SimpleEffectObj::setStateWait));
    } else {
        setNerve(GET_NERVE(SimpleEffectObj, HostTypeMove));
    }

    if (MR::tryRegisterDemoCast(this, rIter)) {
        MR::tryRegisterDemoActionFunctor(this, MR::Functor(this, &SimpleEffectObj::setStateMove), nullptr);
    } else {
        MR::registerDemoSimpleCastAll(this);
    }

    mClippingCenter.set(mPosition + getClippingCenterOffset());
    MR::setClippingTypeSphere(this, getClippingRadius(), &mClippingCenter);
    MR::setGroupClipping(this, rIter, 0x40);
    // An always-on effect (a torch's flame, a lamp's glow) has no logic to wake: it shows wherever the camera sees it.
    // The Comet Observatory's torches were dark until the player came within 50 m. Those that shake the screen when
    // they start keep the level's distance.
    if (mData == nullptr || mData->mShake == SimpleEffectData::cNoShake) {
        MR::setClippingFarMax(this);
    } else {
        MR::setClippingFar(this, getFarClipDistance());
    }
    MR::joinToGroupArray(this, rIter, nullptr, 0x20);
    makeActorAppeared();
}

bool SimpleEffectObj::isStateMove() const {
    return isNerve(GET_NERVE(SimpleEffectObj, HostTypeMove));
}

void SimpleEffectObj::startClipped() {
    LiveActor::startClipped();

    if (isSyncClipping() && isNerve(GET_NERVE(SimpleEffectObj, HostTypeMove))) {
        MR::deleteEffectAll(this);
    }
}

void SimpleEffectObj::endClipped() {
    LiveActor::endClipped();

    if (isSyncClipping() && isNerve(GET_NERVE(SimpleEffectObj, HostTypeMove))) {
        MR::emitEffect(this, mObjName);
    }
}

void SimpleEffectObj::setStateWait() {
    MR::deleteEffect(this, mObjName);
    setNerve(GET_NERVE(SimpleEffectObj, HostTypeWait));
}

void SimpleEffectObj::setStateMove() {
    setNerve(GET_NERVE(SimpleEffectObj, HostTypeMove));
}

void SimpleEffectObj::exeWait() {
}

void SimpleEffectObj::exeMove() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, mObjName);
        if (mData != nullptr) {
            switch (mData->mShake) {
            case 0:
                MR::shakeCameraStrong();
                break;
            case 1:
                MR::shakeCameraNormal();
                break;
            case 2:
                MR::shakeCameraWeak();
                break;
            }

            if (mData->mStartSound != nullptr) {
                MR::startSound(this, mData->mStartSound);
            }
        }
    }

    if (mData == nullptr) {
        return;
    }

    if (mData->mLevelSound != nullptr) {
        MR::startLevelSound(this, mData->mLevelSound);
    }

    if (mData->mRumble != nullptr && mData->mRumbleStep >= 0 && MR::isStep(this, mData->mRumbleStep)) {
        MR::tryRumblePad(this, mData->mRumble, PLAYER_1);
    }
}
