#include "Game/MapObj/StarPieceSpot.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 150.0f);

namespace {
    NEBULA_TUNABLE(f32, sRandomPieceValue, 15.0f);
    NEBULA_TUNABLE(f32, sLaunchYVel, 40.0f);
};  // namespace

StarPieceSpot::StarPieceSpot(const char* pName) : LiveActor(pName) {
}

void StarPieceSpot::emit() {
    MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
    MR::appearStarPiece(this, mPosition, MR::getDeclareRemnantStarPieceCount(this), ::sRandomPieceValue, ::sLaunchYVel, false);
}

void StarPieceSpot::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    s32 starPieceNum = 5;
    MR::getPlacementArg0NoInit(rIter, &starPieceNum);
    MR::declareStarPiece(this, starPieceNum);

    initSound(1, false);

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::listenStageSwitchOnAppear(this, MR::Functor(this, &StarPieceSpot::emit));
        kill();
    } else {
        initHitSensor(1);
        MR::addHitSensorMapObjSimple(this, "body", 8, cNum_init_f1, Vec3f(0.0f, 0.0f, 0.0f));
        appear();
    }
}

bool StarPieceSpot::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgPlayerSpinAttack(msg)) {
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
        MR::appearStarPiece(this, mPosition, MR::getDeclareRemnantStarPieceCount(this), ::sRandomPieceValue, ::sLaunchYVel, false);
        kill();
        return false;
    } else {
        return false;
    }
}
