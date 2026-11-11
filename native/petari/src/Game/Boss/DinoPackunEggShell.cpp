#include "Game/Boss/DinoPackunEggShell.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvDinoPackunEggshell {
    NEW_NERVE(DinoPackunEggShellNrvWait, DinoPackunEggShell, Wait);
};  // namespace NrvDinoPackunEggshell

DinoPackunEggShell::DinoPackunEggShell(const char* pName, HitSensor* pSensor, MtxPtr mtx) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< DinoPackunEggShell >);
    mSensor = pSensor;
    mFollowMtx = mtx;
}

void DinoPackunEggShell::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("DinoPackunEggShell", nullptr, false);
    MR::connectToSceneEnemyDecoration(this);
    MR::initLightCtrl(this);
    MR::initCollisionParts(this, "DinoPackunEggShell", mSensor, nullptr);
    initNerve(GET_NERVE(DinoPackunEggshell, DinoPackunEggShellNrvWait));
    makeActorAppeared();
    MR::startBva(this, "Normal");
    MR::invalidateClipping(this);
}

void DinoPackunEggShell::control() {
    MR::extractMtxTrans(mFollowMtx, &mPosition);
}

void DinoPackunEggShell::calcAndSetBaseMtx() {
    Pos3f mtx;
    mtx.setInline(mFollowMtx);
    mtx.normalizeBasis();
    MR::setBaseTRMtx(this, mtx);
    MR::setBaseScale(this, mScale);
}

void DinoPackunEggShell::exeWait() {
    return;
}
