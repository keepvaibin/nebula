#include "Game/LiveActor/Nerve.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioSwim.hpp"

bool Mario::checkWaterDamage() {
    if (mMovementStates._1B) {
        mMovementStates._1B = 0;
        mSwim->addDamage(mDamageDir);
        return true;
    } else if (mMovementStates._27) {
        mMovementStates._27 = 0;
        mSwim->addFaint(mDamageDir);
        return true;
    } else {
        return false;
    }
}
