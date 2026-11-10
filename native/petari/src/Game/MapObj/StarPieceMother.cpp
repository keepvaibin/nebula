#include "Game/MapObj/StarPieceMother.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/StarPiece.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 100.0f);

namespace {
    // const f32 hMoveSpeed = _;
    NEBULA_TUNABLE(f32, hAddRandomVectorValue, 0.5f);
    NEBULA_TUNABLE(f32, hAddRotateY, 10.0f);
};  // namespace

namespace NrvStarPieceMother {
    NEW_NERVE(HostTypeNrvMoveOnRail, StarPieceMother, MoveOnRail);
    NEW_NERVE(HostTypeNrvWait, StarPieceMother, Wait);
};  // namespace NrvStarPieceMother

StarPieceMother::StarPieceMother(const char* pName) : LiveActor(pName), mStarPieceArray(), mRailCoords(), mNumPieces(6), mNextPiece(), mRailSpeed(10.0f) {
}

void StarPieceMother::init(const TableIter& rIter) {
    s32 isRailNotConnected = false;

    if (!MR::isValidInfo(rIter) || !MR::isConnectedWithRail(rIter)) {
        isRailNotConnected = true;

        MR::initDefaultPos(this, rIter);

        if (MR::useStageSwitchReadAppear(this, rIter)) {
            MR::syncStageSwitchAppear(this);
        }
    } else {
        initRailRider(rIter);
        MR::moveCoordAndTransToRailStartPoint(this);
        MR::needStageSwitchReadAppear(this, rIter);
        MR::syncStageSwitchAppear(this);
    }

    MR::useStageSwitchWriteDead(this, rIter);

    if (MR::useStageSwitchReadA(this, rIter)) {
        MR::listenStageSwitchOnOffA(this, MR::Functor(this, &StarPieceMother::onSwitchA), MR::Functor(this, &StarPieceMother::offSwitchA));
    }

    initModelManagerWithAnm("StarPieceMother", nullptr, false);
    MR::connectToSceneNoSilhouettedMapObj(this);
    MR::onCalcGravity(this);
    MR::initShadowVolumeSphere(this, cNum_init_f1);
    initSound(6, false);
    MR::invalidateClipping(this);
    MR::tryRegisterDemoCast(this, rIter);

    s32 arg = -1;
    MR::getPlacementArg0NoInit(rIter, &arg);
    if (arg != -1) {
        // A count under zero is no pieces.
        mNumPieces = arg > 0 ? arg : 0;
    }

    mStarPieceArray = new StarPiece*[mNumPieces];
    mRailCoords = new f32[mNumPieces];

    for (u32 idx = 0; idx < mNumPieces; idx++) {
        mStarPieceArray[idx] = new StarPiece("スターピースマザーピース");
        mStarPieceArray[idx]->initAndSetFloatingFromGroup(rIter);
        mStarPieceArray[idx]->makeActorDead();

        if (MR::isValidSwitchA(this)) {
            mStarPieceArray[idx]->mFlags.isGettable = false;
        }
    }

    if (isRailNotConnected) {
        initNerve(GET_NERVE(StarPieceMother, HostTypeNrvWait));
        initHitSensor(1);
        MR::addHitSensorMapObj(this, "body", 8, cNum_init_f2, Vec3f(0.0f, 0.0f, 0.0f));
        makeActorAppeared();
    } else {
        initNerve(GET_NERVE(StarPieceMother, HostTypeNrvMoveOnRail));

        arg = -1;
        MR::getPlacementArg1NoInit(rIter, &arg);
        f32 railPos = (arg == -1) ? 0.0f : static_cast< f32 >(arg);
        placementPieceOnRail(railPos);

        arg = -1;
        MR::getPlacementArg2NoInit(rIter, &arg);
        if (arg != -1 && arg != 0) {
            mRailSpeed = static_cast< f32 >(arg);
        }

        makeActorDead();
    }

    MR::startBpk(this, "StarPieceMother");
}

void StarPieceMother::kill() {
    MR::stopSound(this, "SE_OJ_STAR_PIECE_MO_ST");
    MR::startSound(this, "SE_OJ_STAR_PIECE_MO_ED");
    LiveActor::kill();

    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }
}

void StarPieceMother::control() {
    mRotation.y += ::hAddRotateY;
}

void StarPieceMother::exeMoveOnRail() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_STAR_PIECE_MO_ST");
    }

    MR::startLevelSound(this, "SE_OJ_LV_STAR_PIECE_MO_MV");
    MR::moveCoordAndFollowTrans(this, mRailSpeed);

    f32 railCoord = MR::getRailCoord(this);
    // Every piece out, the mother rides on to the rail's end: no piece is left to look up.
    if (static_cast< s32 >(mNextPiece) < mNumPieces && mRailCoords[mNextPiece] <= railCoord) {
        mStarPieceArray[mNextPiece]->appear();
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
        mNextPiece++;
    }

    if (MR::isRailReachedGoal(this)) {
        kill();
    }
}

void StarPieceMother::placementPieceOnRail(f32 distance) {
    // No pieces (placement arg 0 of 0 or less): nothing to place.
    if (mNumPieces <= 0) {
        return;
    }

    Vec3f railPos;
    MR::calcRailPosAtCoord(&railPos, this, distance);
    mStarPieceArray[0]->mPosition.set(railPos);
    mRailCoords[0] = distance;

    f32 step = 0.0f;
    f32 difference = MR::getRailTotalLength(this) - distance;
    if (1 < mNumPieces) {
        step = 1.0f / (mNumPieces - 1.0f);
        step *= difference;
    }

    for (int idx = 1; idx < mNumPieces; idx++) {
        distance += step;
        f32 value = distance >= difference ? difference : distance;
        MR::calcRailPosAtCoord(&railPos, this, value);
        mStarPieceArray[idx]->mPosition.set(railPos);
        mRailCoords[idx] = value;
    }
}

void StarPieceMother::onSwitchA() {
    for (u32 idx = 0; idx < mNumPieces; idx++) {
        mStarPieceArray[idx]->mFlags.isGettable = true;
    }
}

void StarPieceMother::offSwitchA() {
    for (u32 idx = 0; idx < mNumPieces; idx++) {
        mStarPieceArray[idx]->mFlags.isGettable = false;
    }
}

void StarPieceMother::attackSensor(HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver) && (!MR::isValidSwitchA(this) || MR::isOnSwitchA(this))) {
        emitStarPieces();
        kill();
    }
}

void StarPieceMother::emitStarPieces() {
    for (u32 idx = 0; idx < mNumPieces; idx++) {
        Vec3f position;
        position.set(*MR::getPlayerCenterPos());
        position.sub(mPosition);
        MR::normalizeOrZero(&position);
        MR::addRandomVector(&position, position, ::hAddRandomVectorValue);
        MR::normalizeOrZero(&position);

        if (MR::isNearZero(position)) {
            position.set(0.0f, 0.0f, 1.0f);
        }

        mStarPieceArray[idx]->makeActorAppeared();
        mStarPieceArray[idx]->goToPlayer(position);
    }
}

void StarPieceMother::exeWait() {
}
