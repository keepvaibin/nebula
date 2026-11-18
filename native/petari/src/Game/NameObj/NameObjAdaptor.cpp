#include "Game/NameObj/NameObjAdaptor.hpp"
#include "Game/System/LumaCore.hpp"

NameObjAdaptor::NameObjAdaptor(const char* pName)
    : NameObj(pName), mMovementFunc(nullptr), mCalcAnimFunc(nullptr), mCalcViewFunc(nullptr), mDrawAnimFunc(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< NameObjAdaptor >);
}

NameObjAdaptor::~NameObjAdaptor() {
    delete mMovementFunc;
    delete mCalcAnimFunc;
    delete mCalcViewFunc;
    delete mDrawAnimFunc;
}

void NameObjAdaptor::movement() {
    if (mMovementFunc != nullptr) {
        (*mMovementFunc)();
    }
}

void NameObjAdaptor::calcAnim() {
    if (mCalcAnimFunc != nullptr) {
        (*mCalcAnimFunc)();
    }
}

void NameObjAdaptor::calcViewAndEntry() {
    if (mCalcViewFunc != nullptr) {
        (*mCalcViewFunc)();
    }
}

void NameObjAdaptor::draw() const {
    if (mDrawAnimFunc != nullptr) {
        (*mDrawAnimFunc)();
    }
}

void NameObjAdaptor::connectToMovement(const MR::FunctorBase& rFunctor) {
    // The functor connected before is deleted (P-183), after the copy: `rFunctor` may be it.
    MR::FunctorBase* const pBefore = mMovementFunc;
    mMovementFunc = rFunctor.clone(nullptr);
    delete pBefore;
}

void NameObjAdaptor::connectToCalcAnim(const MR::FunctorBase& rFunctor) {
    // The functor connected before is deleted (P-183), after the copy: `rFunctor` may be it.
    MR::FunctorBase* const pBefore = mCalcAnimFunc;
    mCalcAnimFunc = rFunctor.clone(nullptr);
    delete pBefore;
}

void NameObjAdaptor::connectToDraw(const MR::FunctorBase& rFunctor) {
    // The functor connected before is deleted (P-183), after the copy: `rFunctor` may be it.
    MR::FunctorBase* const pBefore = mDrawAnimFunc;
    mDrawAnimFunc = rFunctor.clone(nullptr);
    delete pBefore;
}
