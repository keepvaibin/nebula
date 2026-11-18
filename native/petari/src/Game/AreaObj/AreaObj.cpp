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
#if defined(NEBULA_PLAIN_PATHS)
    AreaObjMgr::sFollowChanges++;
#endif
}

Pos3f* AreaObj::getFollowMtx() const {
    return mForm->mFollowMtx;
}

#if defined(NEBULA_PLAIN_PATHS)
u32 AreaObjMgr::sFollowChanges = 0;
#endif

AreaObjMgr::AreaObjMgr(s32 firstCapacity, const char* pName)
    : NameObj(pName), mArray(), mFirstCapacity(firstCapacity)
#if defined(NEBULA_PLAIN_PATHS)
      ,
      mBounds(), mBoundsMade(0), mBoundsFollow(0)
#endif
{
    lumacore::setClass(this, lumacore::kClassId< AreaObjMgr >);
}

void AreaObjMgr::entry(AreaObj* pAreaObj) {
    if (mArray.capacity() == 0) {
        mArray.init(mFirstCapacity);
    }

    mArray.push_back(pAreaObj);
}

AreaObj* AreaObjMgr::find_in(const Vec3f& rVec) const {
#if defined(NEBULA_PLAIN_PATHS)
    const u32 count = static_cast< u32 >(mArray.size());
    if (mBoundsFollow != sFollowChanges || mBoundsMade > count) {
        mBoundsMade = 0;
        mBoundsFollow = sFollowChanges;
    }
    mBounds.room(count);
    for (; mBoundsMade < count; mBoundsMade++) {
        makeBound(mBoundsMade);
    }

    // The last one added wins.
    for (u32 i = count; i-- > 0;) {
        AreaObj* const pArea = mArray[static_cast< s32 >(i)];
        if (mBounds[i].mObj != pArea) {
            makeBound(i);
        }

        // Written so a NaN anywhere keeps the area.
        const Bound& rBound = mBounds[i];
        if (rBound.mRadiusSquared >= 0.0f && (rVec - rBound.mCenter).squared() > rBound.mRadiusSquared) {
#if defined(NEBULA_AREA_BOUND_VERIFY)
            if (pArea->isInVolume(rVec)) {
                NEBULA_PANIC("area bound left out %s at %f %f %f", pArea->mName, rVec.x, rVec.y, rVec.z);
            }
#endif
            continue;
        }

        if (pArea->isInVolume(rVec)) {
            return pArea;
        }
    }

    return nullptr;
#else
    // The last one added wins.
    for (AreaObj* const* p = mArray.end(); p != mArray.begin();) {
        p--;
        if ((*p)->isInVolume(rVec)) {
            return *p;
        }
    }

    return nullptr;
#endif
}

#if defined(NEBULA_PLAIN_PATHS)
void AreaObjMgr::makeBound(u32 index) const {
    const AreaObj* const pArea = mArray[static_cast< s32 >(index)];
    Bound& rBound = mBounds[index];
    rBound.mObj = pArea;
    rBound.mRadiusSquared = -1.0f;

    Vec3f center;
    f32 radius;
    if (pArea->mForm != nullptr && pArea->mForm->calcBound(&center, &radius) && radius >= 0.0f) {
        // Room over the float rounding of the forms' own tests.
        const f32 room = radius * 1.001f + 1.0f;
        rBound.mCenter = center;
        rBound.mRadiusSquared = room * room;
    }
}
#endif
