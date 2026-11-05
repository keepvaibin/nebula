#pragma once

#include <nebula_geometry.hpp>

class LiveActor;
class WalkerStateParam;

namespace WalkerStateFunction {
    bool isInSightPlayer(const LiveActor* pActor, const Vec3f& rDirection, const WalkerStateParam* pStateParam);

    void calcPassiveMovement(LiveActor* pActor, const WalkerStateParam* pStateParam);
};  // namespace WalkerStateFunction
