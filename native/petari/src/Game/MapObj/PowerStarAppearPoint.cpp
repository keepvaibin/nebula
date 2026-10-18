#include "Game/MapObj/PowerStarAppearPoint.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"

PowerStarAppearPoint::PowerStarAppearPoint(const char* pName) : LiveActor(pName), mCameraInfo() {
}

PowerStarAppearPoint::~PowerStarAppearPoint() {
}

void PowerStarAppearPoint::init(const JMapInfoIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::joinToGroupArray(this, rIter, "\203\160\203\217\201\133\203\130\203\136\201\133\217\157\214\273\203\174\203\103\203\223\203\147\203\117\203\213\201\133\203\166" /* パワースター出現ポイントグループ */, 16);
    MR::initActorCamera(this, rIter, &mCameraInfo);
    makeActorAppeared();
}
