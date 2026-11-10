#include "Game/Enemy/OnimasuJump.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcJumpVelocity_f1, "calcJumpVelocity.f1", 0.5f);

OnimasuJump::OnimasuJump(const char* pName) : Onimasu(pName), mCurNormal(), mNormals() {
}

void OnimasuJump::calcJumpVelocity() {
    Vec3f railPointPos;
    MR::calcRailPointPos(&railPointPos, this, mCurNormal);
    MR::onCalcGravity(this);
    f32 distToMoveThisFrame = mPosition.distance(railPointPos) / getTimeToNextPoint();
    f32 jumpStrength = cNum_calcJumpVelocity_f1 * (getGravityScalar() * getTimeToNextPoint());

    Vec3f dirToRailPointPos(railPointPos - mPosition);

    if (MR::isNearZero(dirToRailPointPos)) {
        dirToRailPointPos.zero();
    } else {
        MR::normalize(&dirToRailPointPos);
    }

    Vec3f gravityDir;
    calcGravityDir(&gravityDir);

    Vec3f moveToRailPos(dirToRailPointPos * distToMoveThisFrame);

    Vec3f moveJump(-gravityDir * jumpStrength);

    mVelocity.set(moveToRailPos + moveJump);
}

s32 OnimasuJump::getLastPointNo() const {
    s32 n = mCurNormal - 1;

    if (n < 0) {
        return MR::getRailPointNum(this) - 1;
    }

    return n;
}

const Vec3f& OnimasuJump::getLastPointNormal() const {
    return mNormals[getLastPointNo()];
}

const Vec3f& OnimasuJump::getNextPointNormal() const {
    return mNormals[mCurNormal];
}

void OnimasuJump::initFromRailPoint() {
    mNormals.init(MR::getRailPointNum(this));
}

void OnimasuJump::incrementNextPoint() {
    mCurNormal++;

    if (mCurNormal == MR::getRailPointNum(this)) {
        mCurNormal = 0;
    }
}

void OnimasuJump::collectRailPointInfo() {
    for (s32 i = 0; i < mNormals.size(); i++) {
        Vec3f normal(gZeroVec);
        Vec3f pos(gZeroVec);
        OnimasuFunction::getPolygonOnRailPoint(&pos, &normal, this, i);
        mNormals[i].set(normal);
    }
}

void OnimasuJump::startMoveInner() {
    calcJumpVelocity();
}

void OnimasuJump::updatePoseInner() {
    Vec3f gravityDir;
    calcGravityDir(&gravityDir);

    mVelocity.add(gravityDir * getGravityScalar());
}

s32 OnimasuJump::getNextPointNo() const {
    return mCurNormal;
}
