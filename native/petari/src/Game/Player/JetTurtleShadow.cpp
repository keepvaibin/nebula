#include "Game/Player/JetTurtleShadow.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Player/PlayerModel.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/System/LumaCore.hpp"

JetTurtleShadow::JetTurtleShadow(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< JetTurtleShadow >);
}

void JetTurtleShadow::calcType0(MtxPtr pMtx) {
    Model* pModel = MR::getModel(this);
    MR::setBaseTRMtx(this, pMtx);
    pModel->calc();
    pModel->viewCalc();
}

void JetTurtleShadow::drawType0() const {
    PlayerModel* pModel = static_cast< PlayerModel* >(MR::getModel(this));

    pModel->mDrawPasses = PlayerModel::Pass_Reflection;

    Frame::setZScaleOffset(1.0f, 0.0f);

    pModel->directDraw();
}

void JetTurtleShadow::drawType1() const {
    PlayerModel* pModel = static_cast< PlayerModel* >(MR::getModel(this));

    pModel->mDrawPasses = PlayerModel::Pass_ScreenCoordinates;

    pModel->directDraw();
}

void JetTurtleShadow::init(const TableIter& rIter) {
    initModelManagerWithAnm("Koura", nullptr, false);

    makeActorDead();
}
