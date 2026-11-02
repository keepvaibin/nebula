#pragma once

#include "Game/NameObj/NameObj.hpp"

class Koopa;
class LiveActor;

class BossAccessor : public NameObj {
public:
    /// @brief Creates a new `BossAccessor`
    BossAccessor();

    LiveActor* getBoss() const {
        return mBoss;
    }

    void setBoss(LiveActor* pBoss) {
        mBoss = pBoss;
    }

private:
    LiveActor* mBoss;
};

namespace BossAccess {
    Koopa* getBossAccessorKoopa();
    void setBossAccessorKoopa(Koopa* pKoopa);
};  // namespace BossAccess
