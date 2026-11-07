#include "Game/Player/JetTurtleShadow.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Player/PlayerModel.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"

JetTurtleShadow::JetTurtleShadow(const char* pName) : LiveActor(pName) {
}

void JetTurtleShadow::calcType0(MtxPtr pMtx) {
    Model* pModel = MR::getModel(this);
    MR::setBaseTRMtx(this, pMtx);
    pModel->calc();
    pModel->viewCalc();
}

void JetTurtleShadow::drawType0() const {
    PlayerModel* pModel = static_cast< PlayerModel* >(MR::getModel(this));

    pModel->mFlags.clear();
    pModel->mFlags._14 = true;

    Frame::setZScaleOffset(1.0f, 0.0f);

    pModel->directDraw();
}

void JetTurtleShadow::drawType1() const {
    PlayerModel* pModel = static_cast< PlayerModel* >(MR::getModel(this));

    pModel->mFlags.clear();
    pModel->mFlags._19 = true;

    pModel->directDraw();
}

void JetTurtleShadow::init(const JMapInfoIter& rIter) {
    initModelManagerWithAnm("Koura", nullptr, false);

    makeActorDead();
}
