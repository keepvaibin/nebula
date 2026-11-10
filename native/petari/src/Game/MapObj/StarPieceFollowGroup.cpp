#include "Game/MapObj/StarPieceFollowGroup.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/StarPiece.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "math_types.hpp"
#include <nebula_geometry.hpp>
#include <nebula_math.h>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_followPieces_f1, "followPieces.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_followPieces_f2, "followPieces.f2", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_followPieces_f3, "followPieces.f3", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_followPieces_f4, "followPieces.f4", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFollowToPlayer_f1, "exeFollowToPlayer.f1", 0.025f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFollowEnd_f1, "exeFollowEnd.f1", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFollowEnd_f2, "exeFollowEnd.f2", 10.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeFollowEnd_1, "exeFollowEnd.1", 20);

namespace NrvStarPieceFollowGroup {
    NEW_NERVE(HostTypeNrvFollowToPlayer, StarPieceFollowGroup, FollowToPlayer);
    NEW_NERVE(HostTypeNrvFollowEnd, StarPieceFollowGroup, FollowEnd);
};  // namespace NrvStarPieceFollowGroup

StarPieceFollowGroup::StarPieceFollowGroup(const char* pName)
    : LiveActor(pName), mPieces(nullptr), mPieceOffsets(nullptr), mNumPiecesTotal(6), mNumPiecesUse(0), mRadius(350.0f), mAngle(0.0f), mTimeLimit(1200) {
}

void StarPieceFollowGroup::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    s32 numPieces = -1;
    MR::getPlacementArg0NoInit(rIter, &numPieces);
    if (numPieces != -1) {
        // A count under zero is no pieces.
        mNumPiecesTotal = numPieces > 0 ? numPieces : 0;
    }

    MR::getPlacementArg1NoInit(rIter, &mTimeLimit);
    MR::getPlacementArg2NoInit(rIter, &mRadius);

    initNerve(GET_NERVE(StarPieceFollowGroup, HostTypeNrvFollowEnd));

    mPieces = new StarPiece*[mNumPiecesTotal];
    mPieceOffsets = new Vec3f[mNumPiecesTotal];
    for (int i = 0; i < mNumPiecesTotal; i++) {
        mPieces[i] = new StarPiece("フォロースターピース");
        mPieces[i]->initWithoutIter();
        mPieces[i]->makeActorDead();
    }

    MR::needStageSwitchWriteA(this, rIter);
    MR::listenStageSwitchOnA(this, MR::Functor(this, &StarPieceFollowGroup::onSwitchA));
    MR::useStageSwitchReadB(this, rIter);

    MR::connectToSceneMapObjMovement(this);
    MR::invalidateClipping(this);
    kill();
}

bool StarPieceFollowGroup::beginFollowPieces() {
    if (MR::isValidSwitchB(this) && MR::isOnSwitchB(this)) {
        return false;
    }

    if (MR::isStageStarPieceFollowGroupLimit()) {
        if (MR::getStarPieceNum() >= 100) {
            return false;
        }

        s32 starPieceNumLeft = MR::getStarPieceNum();
        starPieceNumLeft = 100 - starPieceNumLeft;
        s32 numPiecesToUse;
        if (mNumPiecesTotal >= starPieceNumLeft) {
            numPiecesToUse = starPieceNumLeft;
        } else {
            numPiecesToUse = mNumPiecesTotal;
        }

        mNumPiecesUse = numPiecesToUse;
    } else {
        mNumPiecesUse = mNumPiecesTotal;
    }

    for (int i = 0; i < (u32)mNumPiecesUse; i++) {
        mPieces[i]->setFollowPlayerAndAppear();
    }

    placementPiece(mNumPiecesUse);
    return true;
}

void StarPieceFollowGroup::placementPiece(s32 numPlace) {
    Pos3f mtxTRS;
    MR::makeMtxTRS(mtxTRS.toMtxPtr(), this);

    Vec3f zDir;
    Vec3f xDir;

    mtxTRS.getXDir(xDir);
    mtxTRS.getZDir(zDir);

    Vec3f position(mPosition);
    f32 currentAngle = 0.0f;
    f32 angleBetweenPieces = TWO_PI / numPlace;
    for (u32 i = 0; i < numPlace; i++) {
        f32 zMag = MR::cos(currentAngle) * mRadius;
        f32 xMag = MR::sin(currentAngle) * mRadius;

        mPieces[i]->mPosition = zDir * zMag + xDir * xMag + position;
        currentAngle += angleBetweenPieces;
    }
}

void StarPieceFollowGroup::updateAxises(const Vec3f& rVec1, const Vec3f& rVec2, const Vec3f& rVec3, f32 angle) {
    Quat4f quat;
    quat.setRotate(mLocalXAxis, rVec1, angle);
    quat.rotate(mLocalXAxis);

    quat.setRotate(mLocalZAxis, rVec2, angle);
    quat.rotate(mLocalZAxis);

    quat.setRotate(mLocalYAxis, rVec3, angle);
    quat.rotate(mLocalYAxis);
}

void StarPieceFollowGroup::followPieces() {
    Vec3f playerSideVec;
    Vec3f playerFrontVec;
    Vec3f playerUpVec;

    MR::getPlayerSideVec(&playerSideVec);
    MR::getPlayerFrontVec(&playerFrontVec);
    MR::getPlayerUpVec(&playerUpVec);

    updateAxises(playerSideVec, playerFrontVec, playerUpVec, cNum_followPieces_f1);

    Vec3f playerPos(*MR::getPlayerPos());
    Vec3f playerVelocity(*MR::getPlayerVelocity());

    playerPos.add(mLocalYAxis.multInLine(0.0f));
    playerPos.add(playerVelocity * cNum_followPieces_f2);

    s32 numPiecesNotFollowingAnymore = 0;
    f32 currentAngle = mAngle;
    f32 angleBetweenPieces = TWO_PI / mNumPiecesUse;
    for (u32 i = 0; i < mNumPiecesUse; i++) {
        StarPiece* currentPiece = mPieces[i];
        currentAngle += angleBetweenPieces;
        if (!currentPiece->isFollowing()) {
            numPiecesNotFollowingAnymore++;
            if (numPiecesNotFollowingAnymore == (mNumPiecesUse - 1)) {
                kill();
            }
            continue;
        }

        f32 cos = MR::cos(currentAngle);
        f32 sin = MR::sin(currentAngle);
        f32 wobble = 1.0f + cNum_followPieces_f3 * MR::cos(cNum_followPieces_f4 * currentAngle);

        Vec3f target(playerPos);
        Vec3f offset(0.0f, 0.0f, 0.0f);

        offset.add(mLocalXAxis * cos * mRadius);
        offset.add(mLocalZAxis * sin * mRadius);

        mPieceOffsets[i].set(offset);

        target.add(offset * wobble);
        currentPiece->mVelocity.set(target);
        currentPiece->mVelocity.sub(currentPiece->mPosition);
        currentPiece->mVelocity.mult(1.0f);
    }
}

void StarPieceFollowGroup::allKillPieces() {
    for (int i = 0; i < (u32)mNumPiecesUse; i++) {
        StarPiece* currentPiece = mPieces[i];
        if (!MR::isDead(currentPiece)) {
            currentPiece->makeActorDead();
        }
    }
}

void StarPieceFollowGroup::onSwitchA() {
    if (beginFollowPieces()) {
        setNerve(GET_NERVE(StarPieceFollowGroup, HostTypeNrvFollowToPlayer));
        appear();
    } else {
        kill();
    }
}

void StarPieceFollowGroup::exeFollowToPlayer() {
    if (MR::isFirstStep(this)) {
        MR::getPlayerSideVec(&mLocalXAxis);
        MR::getPlayerFrontVec(&mLocalZAxis);
        MR::getPlayerUpVec(&mLocalYAxis);
    }

    followPieces();
    mAngle += cNum_exeFollowToPlayer_f1;

    if (MR::isGreaterStep(this, mTimeLimit)) {
        setNerve(GET_NERVE(StarPieceFollowGroup, HostTypeNrvFollowEnd));
    }
}

void StarPieceFollowGroup::exeFollowEnd() {
    bool pullBack = false;
    if (MR::isGreaterStep(this, cStep_exeFollowEnd_1)) {
        pullBack = true;
    }

    s32 released = 0;
    for (int i = 0; i < (u32)mNumPiecesUse; i++) {
        StarPiece* currentPiece = mPieces[i];
        if (!currentPiece->isFollowing()) {
            released++;
            if (released == mNumPiecesUse - 1) {
                kill();
            }
            continue;
        }

        if (pullBack) {
            const f32 SCALE = cNum_exeFollowEnd_f1;
            currentPiece->mVelocity += (mPieceOffsets[i] * SCALE);
        }

        Vec3f playerDir(*MR::getPlayerVelocity());
        MR::normalizeOrZero(&playerDir);
        const f32 TEN = cNum_exeFollowEnd_f2;
        currentPiece->mVelocity += (playerDir * TEN);
    }

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        kill();
        MR::offSwitchA(this);
        allKillPieces();
    }
}
