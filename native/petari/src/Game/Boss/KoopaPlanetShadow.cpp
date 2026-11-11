#include "Game/Boss/KoopaPlanetShadow.hpp"
#include "Game/Boss/Koopa.hpp"
#include "Game/Boss/KoopaFunction.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

KoopaPlanetShadow::~KoopaPlanetShadow() {
}

KoopaPlanetShadow::KoopaPlanetShadow(const Koopa* pKoopa) : LiveActor("惑星用の影"), mKoopa(pKoopa), mHipMtx() {
    lumacore::setClass(this, lumacore::kClassId< KoopaPlanetShadow >);
}

void KoopaPlanetShadow::init(const TableIter& /*rIter*/) {
    initModelManagerWithAnm("KoopaPlanetShadow", nullptr, false);

    MR::connectToSceneEnemyDecoration(this);

    MR::invalidateClipping(this);

    mHipMtx = MR::getJointMtx(mKoopa, "Hip1");

    makeActorDead();
}

void KoopaPlanetShadow::calcAndSetBaseMtx() {
    Vec3f vec(mHipMtx[0][3], mHipMtx[1][3], mHipMtx[2][3]);
    Vec3f direction = vec - KoopaFunction::getPlanetCenterPos(mKoopa);

    if (MR::isNearZero(direction)) {
        return;
    }

    MR::normalize(&direction);

    Pos3f transform;
    MR::makeMtxUpFrontPos(&transform, direction, mKoopa->mFront, KoopaFunction::getPlanetCenterPos(mKoopa));
    MR::setBaseTRMtx(this, transform);
}
