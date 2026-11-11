#include "Game/NPC/MiiFaceParts.hpp"
#include "Game/NPC/MiiFacePartsHolder.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/FixedPosition.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/System/LumaCore.hpp"

MiiFaceParts::MiiFaceParts(const char* pName, const MiiFaceRecipe& rRecipe)
    : LiveActor(pName), mRecipe(rRecipe), mFollowMtx(nullptr), mFixedPosition(nullptr), mWaiting(true) {
    lumacore::setClass(this, lumacore::kClassId< MiiFaceParts >);
    MR::createSceneObj(SceneObj_MiiFacePartsHolder);
}

MiiFaceParts::~MiiFaceParts() {
}

void MiiFaceParts::init(const TableIter& /*rIter*/) {
}

void MiiFaceParts::calcAnim() {
    if (mFixedPosition != nullptr) {
        mFixedPosition->calc();
        mPosition.set< f32 >(mFollowMtx[0][3], mFollowMtx[1][3], mFollowMtx[2][3]);
        NebulaMtxCopy(mFollowMtx, mBaseMtx);
    } else {
        MR::makeMtxTR(mBaseMtx, mPosition, mRotation);
    }
    MR::preScaleMtx(mBaseMtx, mScale);
}

void MiiFaceParts::calcViewAndEntry() {
    if (!mHead.isBuilt()) {
        return;
    }
    Mtx viewMtx;
    MR::multMtx(viewMtx, mBaseMtx, ModelSys::mViewMtx);
    mHead.setMatrix(viewMtx);
}

void MiiFaceParts::drawOpa(const Mii::Style& rStyle) const {
    mHead.drawOpa(rStyle);
}

void MiiFaceParts::drawXlu(const Mii::Style& rStyle) const {
    mHead.drawXlu(rStyle);
}

void MiiFaceParts::initFixedPosition(MtxPtr pJoint, const Vec3f& rLocalTrans, const Vec3f& rLocalRotate) {
    mFixedPosition = new FixedPosition(pJoint, rLocalTrans, rLocalRotate);
    mFollowMtx = mFixedPosition->mMtx;
}

void MiiFaceParts::changeExpressionNormal() {
    tryChangeExpression(Mii::ExpressionNormal);
}

void MiiFaceParts::changeExpressionBlink() {
    tryChangeExpression(Mii::ExpressionBlink);
}

void MiiFaceParts::changeFaceModel(const MiiFaceRecipe& rRecipe) {
    mRecipe.mRef = rRecipe.mRef;
    mWaiting = true;
}

void MiiFaceParts::buildIfWaiting() {
    if (!mWaiting) {
        return;
    }
    mWaiting = false;
    Mii::Character mii;
    if (mRecipe.character(&mii)) {
        mHead.build(mii, mRecipe.mExpressions, mRecipe.mFaceSize);
    }
}

bool MiiFaceParts::tryChangeExpression(Mii::Expression expression) {
    if (!mHead.isBuilt() || !mRecipe.hasExpression(expression)) {
        return false;
    }
    mHead.setExpression(expression);
    return true;
}
