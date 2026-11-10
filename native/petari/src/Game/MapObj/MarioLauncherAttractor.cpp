#include "Game/MapObj/MarioLauncherAttractor.hpp"
#include "Game/Gravity/GravityCreator.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 1000.0f);

namespace NrvMarioLauncherAttractor {
    NEW_NERVE(MarioLauncherAttractorNrvWait, MarioLauncherAttractor, Wait);
};  // namespace NrvMarioLauncherAttractor

MarioLauncherAttractor::MarioLauncherAttractor(const char* pName) : LiveActor(pName), mGravityCreator() {
}

void MarioLauncherAttractor::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("MarioLauncherAttractor", nullptr, false);

    // Create and initialize gravity
    mGravityCreator = new PointGravityCreator();
    mGravityCreator->createFromPlacement(rIter);
    PlanetGravity* pGravity = mGravityCreator->getGravity();
    pGravity->mGravityType = GravityType_MarioLauncher;

    // Set clipping & scale
    f32 scale = pGravity->mRange / cNum_init_f1;
    MR::setClippingTypeSphere(this, pGravity->mRange);
    MR::setClippingFar200m(this);

    mScale.set(Vec3f(scale));

    // Connect to executor, init nerve and start animation
    MR::connectToSceneMapObj(this);
    initNerve(GET_NERVE(MarioLauncherAttractor, MarioLauncherAttractorNrvWait));
    MR::startBtk(this, "MarioLauncherAttractor");

    makeActorAppeared();
}

void MarioLauncherAttractor::exeWait() {
}
