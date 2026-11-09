#include "Game/Gravity.hpp"

GravityInfo::GravityInfo() : mGravityVector(0.0f, 0.0f, 0.0f), mGravityInstance(nullptr) {
}

void GravityInfo::init() {
    mGravityVector.zero();
    mGravityInstance = nullptr;
}
