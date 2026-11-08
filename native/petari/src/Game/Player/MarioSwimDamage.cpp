#include "Game/LiveActor/Nerve.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioSwim.hpp"

bool Mario::checkWaterDamage() {
    if (mMovementStates.damaged) {
        mMovementStates.damaged = 0;
        mSwim->addDamage(mDamageDir);
        return true;
    } else if (mMovementStates.fainted) {
        mMovementStates.fainted = 0;
        mSwim->addFaint(mDamageDir);
        return true;
    } else {
        return false;
    }
}
