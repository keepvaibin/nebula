#include "Game/AreaObj/AreaObj.hpp"
#include "Game/AreaObj/AreaForm.hpp"
#include "Game/AreaObj/AreaObjFollower.hpp"
#include "Game/Map/SleepControllerHolder.hpp"
#include "Game/Map/StageSwitch.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

AreaObj::AreaObj(int formType, const char* pName)
    : NameObj(pName), mFormType(formType), mIsValid(true), mIsFollowValid(true), mIsAwake(true), mObjArg0(-1), mObjArg1(-1), mObjArg2(-1), mObjArg3(-1),
      mObjArg4(-1), mObjArg5(-1), mObjArg6(-1), mObjArg7(-1), mSwitchCtrl() {
    lumacore::setClass(this, lumacore::kClassId< AreaObj >);
    switch (formType) {
    case AreaForm::Type_Cube1:
        mForm = new AreaFormCube(0);
        break;
    case AreaForm::Type_Cube2:
        mForm = new AreaFormCube(1);
        break;
    case AreaForm::Type_Sphere:
        mForm = new AreaFormSphere();
        break;
    case AreaForm::Type_Bowl:
        mForm = new AreaFormBowl();
        break;
    case AreaForm::Type_Cylinder:
        mForm = new AreaFormCylinder();
        break;
    }
}

void AreaObj::init(const TableIter& rIter) {
    mForm->init(rIter);
    MR::addBaseMatrixFollowerAreaObj(this, rIter);
    MR::getPlacementArg0WithInit(rIter, &mObjArg0);
    MR::getPlacementArg1WithInit(rIter, &mObjArg1);
    MR::getPlacementArg2WithInit(rIter, &mObjArg2);
    MR::getPlacementArg3WithInit(rIter, &mObjArg3);
    MR::getPlacementArg4WithInit(rIter, &mObjArg4);
    MR::getPlacementArg5WithInit(rIter, &mObjArg5);
    MR::getPlacementArg6WithInit(rIter, &mObjArg6);
    MR::getPlacementArg7WithInit(rIter, &mObjArg7);

    mSwitchCtrl = MR::createStageSwitchCtrl(this, rIter);

    if (mSwitchCtrl->isValidSwitchAppear()) {
        MR::listenNameObjStageSwitchOnOffAppear(this, mSwitchCtrl, MR::Functor(this, &AreaObj::validate), MR::Functor(this, &AreaObj::invalidate));
        mIsValid = false;
    }

    // The manager by the placed name, as the game takes it (a game area is made under its Japanese name, which no
    // manager's name heads). A mod's area class is made by its maker row (LumaAreaCube, LumaAreaSphere,
    // LumaAreaCylinder: manager LumaArea) and placed under the name its mod aliased, which no manager's name heads:
    // its manager is the one the name it was made with heads.
    AreaObjMgr* const pMadeManager = MR::getAreaObjManager(getManagerName());

    const char* name;

    if (MR::getObjectName(&name, rIter)) {
        setName(name);
    }

    AreaObjMgr* const pPlacedManager = MR::getAreaObjManager(getManagerName());
    AreaObjMgr* const pManager = pPlacedManager != nullptr ? pPlacedManager : pMadeManager;
    pManager->entry(this);
    SleepControlFunc::addSleepControl(this, rIter);
}

bool AreaObj::isInVolume(const Vec3f& rPos) const {
    return isValid() && mForm->isInVolume(rPos);
}

void AreaObj::onSwitchA() {
    mSwitchCtrl->onSwitchA();
}

void AreaObj::offSwitchA() {
    mSwitchCtrl->offSwitchA();
}

bool AreaObj::isOnSwitchA() const {
    return mSwitchCtrl->isOnSwitchA();
}

bool AreaObj::isOnSwitchB() const {
    return mSwitchCtrl->isOnSwitchB();
}

bool AreaObj::isValidSwitchA() const {
    return mSwitchCtrl->isValidSwitchA();
}

bool AreaObj::isValidSwitchB() const {
    return mSwitchCtrl->isValidSwitchB();
}

void AreaObj::setFollowMtx(const Pos3f* pMtx) {
    mForm->mFollowMtx = const_cast< Pos3f* >(pMtx);
}

Pos3f* AreaObj::getFollowMtx() const {
    return mForm->mFollowMtx;
}

AreaObjMgr::AreaObjMgr(s32 firstCapacity, const char* pName) : NameObj(pName), mArray(), mFirstCapacity(firstCapacity) {
    lumacore::setClass(this, lumacore::kClassId< AreaObjMgr >);
}

void AreaObjMgr::entry(AreaObj* pAreaObj) {
    if (mArray.capacity() == 0) {
        mArray.init(mFirstCapacity);
    }

    mArray.push_back(pAreaObj);
}

AreaObj* AreaObjMgr::find_in(const Vec3f& rVec) const {
    // The last one added wins.
    for (AreaObj* const* p = mArray.end(); p != mArray.begin();) {
        p--;
        if ((*p)->isInVolume(rVec)) {
            return *p;
        }
    }

    return nullptr;
}
