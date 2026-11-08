#include "Game/Boss/DodoryuStateBase.hpp"
#include "Game/Boss/Dodoryu.hpp"
#include "Game/Boss/DodoryuUtil.hpp"
#include "Game/Util/MathUtil.hpp"

void DodoryuStateBase::calcAttackDir(Vec3f* pVec, const Vec3f& rVec1, const Vec3f& rVec2) {
    pVec->sub(rVec2, rVec1);

    pVec->orthogonalize(mHost->mGravity);
    MR::normalizeOrZero(pVec);

    if (MR::isNearZero(*pVec)) {
        mHost->mBaseMtx.getXDir(*pVec);
    }

    Vec3f yDir;
    mHost->mBaseMtx.getYDir(yDir);
    pVec->add(yDir);
    MR::normalize(pVec);
}

bool DodoryuStateBase::calcVerticalizedDir(Vec3f* pVec, const Vec3f& rVec) {
    Vec3f yDir;
    mHost->mBaseMtx.getYDir(yDir);

    pVec->killElement(rVec, yDir);

    if (MR::isNearZero(*pVec)) {
        return false;
    }

    MR::normalize(pVec);

    return true;
}
