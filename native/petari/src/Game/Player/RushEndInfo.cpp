#include "Game/Player/RushEndInfo.hpp"

RushEndInfo::RushEndInfo(LiveActor* pActor, Mode mode, const Vec3f& rVec, bool keepAnimation, u32 blendFrames)
    : mActor(pActor), mMode(mode), mVec(rVec), mKeepAnimation(keepAnimation), mBlendFrames(blendFrames) {
}
