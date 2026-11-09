#include "Game/Boss/TombSpiderAcid.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/TombSpider.hpp"
#include "Game/Boss/TombSpiderFunction.hpp"
#include "Game/Boss/TombSpiderGland.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"

namespace {
    const s32 sStepToStartAcidGround = NebulaFrameClock::stepsOfMilliseconds(500);
    const f32 sDistanceCheckMap = 2500.0f;
    const s32 sStepToEndAcidGround = NebulaFrameClock::stepsOfMilliseconds(500);
};  // namespace

TombSpiderAcid::TombSpiderAcid(TombSpider* pParent) : mParent(pParent), mIsEmitOn(), mIsActive(), mStateChangeTimer() {
    mMouthEffectMtx.identity();
    mGlandFrontLEffectMtx.identity();
    mGlandFrontREffectMtx.identity();
    mGlandRearLEffectMtx.identity();
    mGlandRearREffectMtx.identity();

    MR::setEffectHostMtx(mParent, "AcidSplash", mMouthEffectMtx);
    MR::setEffectHostMtx(TombSpiderFunction::getGlandFrontL(mParent), "AcidSplash", mGlandFrontLEffectMtx);
    MR::setEffectHostMtx(TombSpiderFunction::getGlandFrontR(mParent), "AcidSplash", mGlandFrontREffectMtx);
    MR::setEffectHostMtx(TombSpiderFunction::getGlandRearL(mParent), "AcidSplash", mGlandRearLEffectMtx);
    MR::setEffectHostMtx(TombSpiderFunction::getGlandRearR(mParent), "AcidSplash", mGlandRearREffectMtx);
}

void TombSpiderAcid::start() {
    mIsEmitOn = true;
    mStateChangeTimer = 0;
}

void TombSpiderAcid::startGland() {
    mIsEmitOn = true;
    mStateChangeTimer = 0;
}

void TombSpiderAcid::end() {
    mIsEmitOn = false;
    mStateChangeTimer = 0;
}

void TombSpiderAcid::update() {
    if (mIsEmitOn) {
        updateEmitAcid();
    } else {
        updateDeleteAcid();
    }

    if (mIsActive) {
        updateGroundMtx(&mMouthEffectMtx, TombSpiderFunction::getMtxMouth(mParent));

        updateGlandGroundMtx(TombSpiderFunction::getGlandFrontL(mParent), &mGlandFrontLEffectMtx, TombSpiderFunction::getMtxGlandFrontL(mParent));
        updateGlandGroundMtx(TombSpiderFunction::getGlandFrontR(mParent), &mGlandFrontREffectMtx, TombSpiderFunction::getMtxGlandFrontR(mParent));
        updateGlandGroundMtx(TombSpiderFunction::getGlandRearL(mParent), &mGlandRearLEffectMtx, TombSpiderFunction::getMtxGlandRearL(mParent));
        updateGlandGroundMtx(TombSpiderFunction::getGlandRearR(mParent), &mGlandRearREffectMtx, TombSpiderFunction::getMtxGlandRearR(mParent));
    }
}

void TombSpiderAcid::updateGlandGroundMtx(const TombSpiderGland* pGland, Pos3f* pDstMtx, MtxPtr pSrcMtx) {
    if (pGland->isActive() && !pGland->isDamage()) {
        updateGroundMtx(pDstMtx, pSrcMtx);
    }
}

namespace {
    void tryEmitGlandAcid(TombSpiderGland* pGland) {
        if (pGland->isActive()) {
            MR::emitEffect(pGland, "AcidSplash");
        }
    }
};  // namespace

void TombSpiderAcid::updateEmitAcid() {
    if (!mIsActive && ++mStateChangeTimer >= ::sStepToStartAcidGround) {
        MR::emitEffect(mParent, "AcidSplash");

        ::tryEmitGlandAcid(TombSpiderFunction::getGlandFrontL(mParent));
        ::tryEmitGlandAcid(TombSpiderFunction::getGlandFrontR(mParent));
        ::tryEmitGlandAcid(TombSpiderFunction::getGlandRearL(mParent));
        ::tryEmitGlandAcid(TombSpiderFunction::getGlandRearR(mParent));

        mIsActive = true;
    }
}

void TombSpiderAcid::updateDeleteAcid() {
    if (mIsActive && ++mStateChangeTimer >= ::sStepToEndAcidGround) {
        MR::deleteEffect(mParent, "AcidSplash");
        MR::deleteEffect(TombSpiderFunction::getGlandFrontL(mParent), "AcidSplash");
        MR::deleteEffect(TombSpiderFunction::getGlandFrontR(mParent), "AcidSplash");
        MR::deleteEffect(TombSpiderFunction::getGlandRearL(mParent), "AcidSplash");
        MR::deleteEffect(TombSpiderFunction::getGlandRearR(mParent), "AcidSplash");

        mIsActive = false;
    }
}

void TombSpiderAcid::updateGroundMtx(Pos3f* pDstMtx, MtxPtr pSrcMtx) {
    Vec3f up(-pSrcMtx[0][2], -pSrcMtx[1][2], -pSrcMtx[2][2]);
    Vec3f front(0.0f, 0.0f, 1.0f);
    // Up crossed with front; a joint facing along front gets a perpendicular instead of a zero side.
    Vec3f side;
    MR::crossOrPerpendicular(&side, up, front);

    Vec3f checkLine(up);
    checkLine.scale(-::sDistanceCheckMap);
    Vec3f mtxPos(pSrcMtx[0][3], pSrcMtx[1][3], pSrcMtx[2][3]);
    Vec3f trans(0.0f, 0.0f, 0.0f);
    MR::getFirstPolyOnLineToMap(&trans, nullptr, mtxPos, checkLine);

    pDstMtx->setTR(side, up, front, trans);
}
