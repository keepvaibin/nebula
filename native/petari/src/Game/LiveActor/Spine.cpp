#include "Game/LiveActor/Spine.hpp"
#include "Game/LiveActor/ActorStateKeeper.hpp"
#include "Game/LiveActor/Nerve.hpp"

Spine::Spine(void* pExecutor, const Nerve* pNerve) : mExecutor(pExecutor), mCurrNerve(pNerve), mNextNerve(), mStep(), mStateKeeper() {
}

// A nerve a plugin hooks has mNebulaHook set (NerveRegistry.cpp): one load beside the nerve's vtable, and a branch, for the rest.
void Spine::update() {
    changeNerve();
    if (mCurrNerve->mNebulaHook == 0 || nebula::gNerveHook(this, mCurrNerve, nebula::NervePhase_Frame)) {
        mCurrNerve->execute(this);
    }
    mStep++;
    changeNerve();
}

void Spine::setNerve(const Nerve* pNerve) {
    if (mStep >= 0) {
        mCurrNerve->executeOnEnd(this);
        if (mCurrNerve->mNebulaHook != 0) {
            nebula::gNerveHook(this, mCurrNerve, nebula::NervePhase_Leave);
        }
    }

    mNextNerve = pNerve;
    mStep = -1;
}

const Nerve* Spine::getCurrentNerve() const {
    if (mNextNerve != nullptr) {
        return mNextNerve;
    }

    return mCurrNerve;
}

void Spine::changeNerve() {
    if (mNextNerve == nullptr) {
        return;
    }

    if (mStateKeeper != nullptr) {
        mStateKeeper->endState(mCurrNerve);
        mStateKeeper->startState(mNextNerve);
    }

    mCurrNerve = mNextNerve;
    mNextNerve = nullptr;
    mStep = 0;
    if (mCurrNerve->mNebulaHook != 0) {
        nebula::gNerveHook(this, mCurrNerve, nebula::NervePhase_Enter);
    }
}

void Spine::initStateKeeper() {
    mStateKeeper = new ActorStateKeeper();
}
