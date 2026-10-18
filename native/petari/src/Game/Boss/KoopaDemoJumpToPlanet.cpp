#include "Game/Boss/KoopaDemoJumpToPlanet.hpp"
#include "Game/Boss/Koopa.hpp"
#include "Game/Boss/KoopaBattleBase.hpp"
#include "Game/Boss/KoopaFunction.hpp"
#include "Game/Boss/KoopaPlanetShadow.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace {
    static MR::ActorMoveParam sFallParam = {0.0f, 1.0f, 0.98f, 0.0f};
    static MR::ActorMoveParam sWaitParam = {0.0f, 1.0f, 0.9f, 1.0f};
    static const s32 sFallWarpStepVs3 = 90;
};  // namespace

namespace NrvKoopaDemoJumpToPlanet {
    NEW_NERVE(KoopaDemoJumpToPlanetNrvStart, KoopaDemoJumpToPlanet, Start);
    NEW_NERVE(KoopaDemoJumpToPlanetNrvFall, KoopaDemoJumpToPlanet, Fall);
    NEW_NERVE(KoopaDemoJumpToPlanetNrvLand, KoopaDemoJumpToPlanet, Land);
    NEW_NERVE(KoopaDemoJumpToPlanetNrvWaitPlayer, KoopaDemoJumpToPlanet, WaitPlayer);
};  // namespace NrvKoopaDemoJumpToPlanet

KoopaDemoJumpToPlanet::KoopaDemoJumpToPlanet(Koopa* pKoopa) : ActorStateBase< Koopa >("Demo[\230\146\220\257\202\334\202\305\203\127\203\203\203\223\203\166]" /* Demo[惑星までジャンプ] */, pKoopa) {
}

void KoopaDemoJumpToPlanet::init() {
    KoopaFunction::initKoopaCamera(mHost, "\203\105\203\106\203\103\203\147\201\151\230\146\220\257\202\334\202\305\203\127\203\203\203\223\203\166\201\152" /* ウェイト（惑星までジャンプ） */);
    KoopaFunction::initKoopaCamera(mHost, "\227\216\211\272\201\151\230\146\220\257\202\334\202\305\203\127\203\203\203\223\203\166\201\152" /* 落下（惑星までジャンプ） */);
    KoopaFunction::initKoopaCamera(mHost, "\203\217\201\133\203\166\214\343\201\151\230\146\220\257\202\334\202\305\203\127\203\203\203\223\203\166\201\152" /* ワープ後（惑星までジャンプ） */);
    initNerve(GET_NERVE(KoopaDemoJumpToPlanet, KoopaDemoJumpToPlanetNrvStart));
}

void KoopaDemoJumpToPlanet::kill() {
    mIsDead = true;

    KoopaFunction::endKoopaCamera(mHost, "\203\105\203\106\203\103\203\147\201\151\230\146\220\257\202\334\202\305\203\127\203\203\203\223\203\166\201\152" /* ウェイト（惑星までジャンプ） */, false);
    KoopaFunction::endKoopaCamera(mHost, "\227\216\211\272\201\151\230\146\220\257\202\334\202\305\203\127\203\203\203\223\203\166\201\152" /* 落下（惑星までジャンプ） */, false);
    KoopaFunction::endKoopaCamera(mHost, "\203\217\201\133\203\166\214\343\201\151\230\146\220\257\202\334\202\305\203\127\203\203\203\223\203\166\201\152" /* ワープ後（惑星までジャンプ） */, false);
}

void KoopaDemoJumpToPlanet::startReady() {
    MR::onSwitchB(mHost);
}

void KoopaDemoJumpToPlanet::exeStart() {
    if (MR::isFirstStep(this)) {
        KoopaFunction::setKoopaPos(mHost, "\220\355\223\254\212\112\216\156\201\151\203\116\203\142\203\160\201\152" /* 戦闘開始（クッパ） */);
        MR::setPlayerPosAndWait("\220\355\223\254\212\112\216\156\201\151\203\175\203\212\203\111\201\152" /* 戦闘開始（マリオ） */);

        KoopaFunction::startKoopaCamera(mHost, "\227\216\211\272\201\151\230\146\220\257\202\334\202\305\203\127\203\203\203\223\203\166\201\152" /* 落下（惑星までジャンプ） */);
        MR::startAction(mHost, "JumpSoon");
        KoopaFunction::startFaceCtrl(mHost);

        setNerve(GET_NERVE(KoopaDemoJumpToPlanet, KoopaDemoJumpToPlanetNrvFall));
    }
}

void KoopaDemoJumpToPlanet::exeFall() {
    if (MR::isFirstStep(this) && KoopaFunction::getKoopaPlanetShadow(mHost)) {
        KoopaFunction::getKoopaPlanetShadow(mHost)->appear();
    }

    MR::moveAndTurnToPlayer(mHost, &mHost->mFront, ::sFallParam);

    if (KoopaFunction::isKoopaVs3(mHost) && MR::isStep(this, ::sFallWarpStepVs3)) {
        KoopaFunction::setKoopaPos(mHost, "\202\153\202\226\202\120\212\112\216\156\201\151\203\116\203\142\203\160\201\152" /* Ｌｖ１開始（クッパ） */);
        MR::setPlayerPosAndWait("\202\153\202\226\202\120\212\112\216\156\201\151\203\175\203\212\203\111\201\152" /* Ｌｖ１開始（マリオ） */);

        KoopaFunction::endKoopaCamera(mHost, "\227\216\211\272\201\151\230\146\220\257\202\334\202\305\203\127\203\203\203\223\203\166\201\152" /* 落下（惑星までジャンプ） */, false);
        KoopaFunction::startKoopaCamera(mHost, "\203\217\201\133\203\166\214\343\201\151\230\146\220\257\202\334\202\305\203\127\203\203\203\223\203\166\201\152" /* ワープ後（惑星までジャンプ） */);
    }

    MR::startLevelSound(mHost, "SE_BM_LV_KOOPA_FALL_TO_PLANET");

    if (MR::isBindedGround(mHost)) {
        MR::zeroVelocity(mHost);

        setNerve(GET_NERVE(KoopaDemoJumpToPlanet, KoopaDemoJumpToPlanetNrvLand));
    }
}

void KoopaDemoJumpToPlanet::exeLand() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "Land");
        MR::startSound(mHost, "SE_BM_KOOPA_FLIP_RECOVER_END");
        MR::startSound(mHost, "SE_BV_KOOPA_LAND_HEAVY");

        MR::tryRumblePadStrong(mHost, WPAD_CHAN0);
        MR::shakeCameraNormalStrong();
    }

    if (MR::isActionEnd(mHost)) {
        setNerve(GET_NERVE(KoopaDemoJumpToPlanet, KoopaDemoJumpToPlanetNrvWaitPlayer));
    }
}

void KoopaDemoJumpToPlanet::exeWaitPlayer() {
    if (MR::isFirstStep(this)) {
        KoopaFunction::startFaceCtrl(mHost);
    }

    MR::moveAndTurnToPlayer(mHost, &mHost->mFront, ::sWaitParam);

    if (MR::isOnGroundPlayer()) {
        KoopaFunction::startFaceCtrl(mHost);

        kill();
    }
}

KoopaDemoJumpToPlanet::~KoopaDemoJumpToPlanet() {
}
