#include "Game/MapObj/PrizeRing.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 300.0f);

namespace {
    // By the ring's number; the rest play ring 1.
    const char* const sPlaySoundNames[] = {nullptr, "SE_SY_GET_PRIZE_RING_5", "SE_SY_GET_PRIZE_RING_4", "SE_SY_GET_PRIZE_RING_3",
                                           "SE_SY_GET_PRIZE_RING_2"};
    NEBULA_TUNABLE_LIST(f32, sOnTriggerFlashFrames, , {10.0f, 60.0f, 110.0f, 160.0f, 210.0f, 240.0f, 270.0f, 300.0f, -1.0f});
    NEBULA_TUNABLE(s32, sDefaultLifeTime, 800);
};  // namespace

namespace NrvPrizeRing {
    NEW_NERVE(PrizeRingStart, PrizeRing, Start);
    NEW_NERVE(PrizeRingLoop, PrizeRing, Loop);
    NEW_NERVE(PrizeRingTimeout, PrizeRing, Timeout);
    NEW_NERVE(PrizeRingReadyToPass, PrizeRing, ReadyToPass);
    NEW_NERVE(PrizeRingPass, PrizeRing, Pass);
    NEW_NERVE(PrizeRingReadyToKill, PrizeRing, ReadyToKill);
};  // namespace NrvPrizeRing

PrizeRing::PrizeRing() : ModelObj("PrizeRing", "PrizeRing", nullptr, -2, -2, -2, false), mLifeTime(::sDefaultLifeTime) {
}

void PrizeRing::init(const TableIter& /*rIter*/) {
    initNerve(GET_NERVE(PrizeRing, PrizeRingStart));
    initHitSensor(1);
    MR::addHitSensorMapObjSimple(this, "body", 4, cNum_init_f1, Vec3f(0.0f, 0.0f, 0.0f));
    makeActorDead();
}

void PrizeRing::appear() {
    makeActorAppeared();
    MR::showModel(this);
    MR::invalidateClipping(this);
    setNerve(GET_NERVE(PrizeRing, PrizeRingStart));
}

void PrizeRing::kill() {
    makeActorDead();
}

void PrizeRing::setLife(s32 lifeTime) {
    mLifeTime = lifeTime;
}

// The bva frame holds the number shown, from 1.
void PrizeRing::setNumber(s32 num) {
    f32 frame = num - 1.0f;
    MR::startBva(this, "Number");
    MR::setBvaFrameAndStop(this, frame);
}

void PrizeRing::setNervePass() {
    setNerve(GET_NERVE(PrizeRing, PrizeRingPass));
}

bool PrizeRing::isReadyToPass() const {
    return !MR::isDead(this) && isNerve(GET_NERVE(PrizeRing, PrizeRingReadyToPass));
}

bool PrizeRing::isReadyToKill() const {
    return !MR::isDead(this) && isNerve(GET_NERVE(PrizeRing, PrizeRingReadyToKill));
}

void PrizeRing::exeStart() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_PRIZE_RING_APPEAR");
        MR::startBck(this, "Start");
        MR::startBrk(this, "Loop");
        MR::setBrkFrameAndStop(this, 0.0f);
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(PrizeRing, PrizeRingLoop));
    }
}

void PrizeRing::exeLoop() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Loop");
        MR::startBrk(this, "Loop");
    }

    if (MR::isStep(this, mLifeTime)) {
        setNerve(GET_NERVE(PrizeRing, PrizeRingTimeout));
    }
}

void PrizeRing::exeTimeout() {
    if (MR::isFirstStep(this)) {
        MR::startBrk(this, "Timeout");
    }

    if (isOnTriggerTimeoutFlash()) {
        MR::startSound(this, "SE_OJ_PRIZE_RING_BLINK");
    }

    if (MR::isBrkStopped(this)) {
        MR::startSound(this, "SE_OJ_PRIZE_RING_DISAPPEAR");
        kill();
    }
}

void PrizeRing::exeReadyToPass() {
}

void PrizeRing::exePass() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "End");
        MR::startBrk(this, "End");
        MR::tryRumblePadMiddle(this, PLAYER_1);
        playSound();
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(PrizeRing, PrizeRingReadyToKill));
    }
}

void PrizeRing::exeReadyToKill() {
    if (MR::isFirstStep(this)) {
        MR::hideModel(this);
    }
}

bool PrizeRing::isPassed() const {
    return isReadyToPass() || isNerve(GET_NERVE(PrizeRing, PrizeRingPass)) || isReadyToKill();
}

void PrizeRing::attackSensor(HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver) && !isPassed()) {
        setNerve(GET_NERVE(PrizeRing, PrizeRingReadyToPass));
    }
}

s32 PrizeRing::getNumber() const {
    return static_cast< s32 >(MR::getBvaCtrl(this)->mFrame) + 1;
}

void PrizeRing::playSound() const {
    const s32 number = getNumber();
    const char* pSound = static_cast< u32 >(number) < ARRAY_SIZE(::sPlaySoundNames) ? ::sPlaySoundNames[number] : nullptr;

    MR::startSystemSE(pSound != nullptr ? pSound : "SE_SY_GET_PRIZE_RING_1");
    if (number == 1) {
        MR::startSystemSE("SE_SY_TOTAL_COMPLETE");
    }
}

// True on the step the brk frame passes one of the flash frames.
bool PrizeRing::isOnTriggerTimeoutFlash() const {
    if (!isNerve(GET_NERVE(PrizeRing, PrizeRingTimeout))) {
        return false;
    }

    const FrameCtrl* pBrkCtrl = MR::getBrkCtrl(this);
    for (u32 i = 0; i < ARRAY_SIZE(::sOnTriggerFlashFrames); i++) {
        const f32 flashFrame = ::sOnTriggerFlashFrames[i];
        const f32 frame = pBrkCtrl->mFrame;
        if (frame <= flashFrame && flashFrame < frame + pBrkCtrl->mRate) {
            return true;
        }
    }

    return false;
}
