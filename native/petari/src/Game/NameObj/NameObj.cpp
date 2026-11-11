#include "Game/NameObj/NameObj.hpp"
#include "Game/NameObj/NameObjHolder.hpp"
#include "Game/NameObj/NameObjRegister.hpp"
#include "Game/Scene/SceneNameObjMovementController.hpp"
#include "Game/Util/SingletonHolder.hpp"

NameObj::NameObj(const char* pName) : mName(pName), mMovementFlag(), mLumaClass(0), mExecutorIdx(-1) {
    mMotionEpoch = 0;
    mDrawSlot = 0;
    mListPlace[0] = -1;
    mListPlace[1] = -1;
    mListPlace[2] = -1;
    mDrawBufferPlace = -1;
    mNameHolder = nullptr;
    mHolderPlace = -1;
    SingletonHolder< NameObjRegister >::get()->add(this);
}

NameObj::~NameObj() {
}

void NameObj::init(const TableIter& /*rIter*/) {
}

void NameObj::initAfterPlacement() {
}

void NameObj::movement() {
}

void NameObj::draw() const {
}

void NameObj::calcAnim() {
}

void NameObj::calcViewAndEntry() {
}

void NameObj::initWithoutIter() {
    init(TableIter());
}

void NameObj::setName(const char* pName) {
    const char* pOld = mName;
    mName = pName;
    if (mNameHolder != nullptr) {
        mNameHolder->renamed(this, pOld);
    }
}

void NameObj::executeMovement() {
    if ((mMovementFlag & MovementFlag_Off) != 0) {
        return;
    }

    movement();
}

// The later request wins.
void NameObj::requestSuspend() {
    mMovementFlag &= ~MovementFlag_ResumeRequest;
    mMovementFlag |= MovementFlag_SuspendRequest;
}

void NameObj::requestResume() {
    mMovementFlag &= ~MovementFlag_SuspendRequest;
    mMovementFlag |= MovementFlag_ResumeRequest;
}

void NameObj::syncWithFlags() {
    if ((mMovementFlag & MovementFlag_SuspendRequest) != 0) {
        mMovementFlag &= ~MovementFlag_SuspendRequest;
        mMovementFlag |= MovementFlag_Off;
    }

    if ((mMovementFlag & MovementFlag_ResumeRequest) != 0) {
        mMovementFlag &= ~(MovementFlag_ResumeRequest | MovementFlag_Off);
    }
}

void NameObjFunction::requestMovementOn(NameObj* pObj) {
    pObj->requestResume();
    MR::notifyRequestNameObjMovementOnOff();
}

void NameObjFunction::requestMovementOff(NameObj* pObj) {
    pObj->requestSuspend();
    MR::notifyRequestNameObjMovementOnOff();
}
