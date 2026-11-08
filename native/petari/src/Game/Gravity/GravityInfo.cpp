#include "Game/Gravity.hpp"

GravityInfo::GravityInfo() : mGravityVector(0.0f, 0.0f, 0.0f), mLargestPriority(-1), mGravityInstance(nullptr) {
}

void GravityInfo::init() {
    mLargestPriority = -1;
    mGravityVector.zero();
    mGravityInstance = nullptr;
}
