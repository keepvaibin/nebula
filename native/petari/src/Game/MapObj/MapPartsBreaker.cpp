#include "Game/MapObj/MapPartsBreaker.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"

namespace NrvMapPartsBreaker {
    NEW_NERVE(HostTypeWait, MapPartsBreaker, Wait);
    NEW_NERVE(HostTypeReactToTornado, MapPartsBreaker, ReactToTornado);
    NEW_NERVE(HostTypeBreak, MapPartsBreaker, Break);
};  // namespace NrvMapPartsBreaker

void MapPartsBreaker::exeWait() {
}

void MapPartsBreaker::exeReactToTornado() {
    if (isFirstStep()) {
        Vec3f* vel = &mHost->mVelocity;
        vel->set((_28 * 5.0f) * _34);
    }

    if (getStep() % 3 == 0) {
        mHost->mVelocity.negate();
    }

    if (isStep(36)) {
        if (_38) {
            setNerve(GET_NERVE(MapPartsBreaker, HostTypeBreak));
        } else {
            mHost->mVelocity.zero();
            mHost->mPosition.set(_1C);
            _28.x = 0.0f;
            _28.y = 1.0f;
            _28.z = 0.0f;
            setNerve(GET_NERVE(MapPartsBreaker, HostTypeWait));
        }
    } else {
        _38 = 0;
    }
}

void MapPartsBreaker::exeBreak() {
    if (isFirstStep()) {
        mHost->mVelocity.zero();
        MR::emitEffect(mHost, "SmokeSphere");
        MR::hideModel(mHost);
        MR::invalidateCollisionParts(mHost);
    }

    if (isStep(15)) {
        sendMsgToHost(206);
        mHost->kill();
    }
}

