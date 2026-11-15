#include "Game/Map/FileSelectItem.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/Map/FileSelectIconID.hpp"
#include "Game/Map/FileSelectItemDelegator.hpp"
#include "Game/Map/FileSelectModel.hpp"
#include "Game/NPC/MiiFaceParts.hpp"
#include "Game/NPC/MiiFacePartsHolder.hpp"
#include "Game/NPC/MiiFaceRecipe.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/FileSelectNumber.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    // The icon's size ease lasts half a second.
    const s32 cScaleEaseSteps = NebulaFrameClock::stepsOfSeconds(1) / 2;

    NEW_NERVE(FileSelectItemNrvNewWait, FileSelectItem, NewWait);
    NEW_NERVE(FileSelectItemNrvExistWait, FileSelectItem, ExistWait);
    NEW_NERVE(FileSelectItemNrvFormat, FileSelectItem, Format);
    NEW_NERVE(FileSelectItemNrvChangeFellow, FileSelectItem, ChangeFellow);
    NEW_NERVE(FileSelectItemNrvChangeMii, FileSelectItem, ChangeMii);

    // rAxis runs from rBase along the cylinder; its length is the cylinder's.
    bool checkCollisionOfPointAndCylinder(const Vec3f& rPoint, const Vec3f& rBase, const Vec3f& rAxis, f32 radius) {
        f32 length = rAxis.length();
        Vec3f vecCopy(rAxis);
        MR::normalize(&vecCopy);

        Vec3f diff(rPoint - rBase);

        f32 dot = vecCopy.dot(diff);
        if (dot < 0.0f || dot > length) {
            return false;
        }

        vecCopy.scale(dot);
        return !(vecCopy.distance(diff) > radius);
    }

    const char* const sFellowModel[] = {"FileSelectDataMario", "FileSelectDataLuigi", "FileSelectDataYoshi", "FileSelectDataKinopio",
                                        "FileSelectDataPeach"};
    const s32 sFellowNum = ARRAY_SIZE(sFellowModel);

    const Vec sDataInfoOffset = {0.0f, 2150.0f, 0.0f};
};  // namespace

namespace FileSelectItemSub {
    NEW_NERVE(ScaleControllerNrvToSmall, ScaleController, ToSmall);
    NEW_NERVE(ScaleControllerNrvSmall, ScaleController, Small);
    NEW_NERVE(ScaleControllerNrvToBig, ScaleController, ToBig);
    NEW_NERVE(ScaleControllerNrvBig, ScaleController, Big);
    NEW_NERVE(BlinkControllerNrvOpen, BlinkController, Open);
    NEW_NERVE(BlinkControllerNrvShut, BlinkController, Shut);
    NEW_NERVE(BlinkControllerNrvSleep, BlinkController, Sleep);
    NEW_NERVE(BlinkControllerNrvBlink, BlinkController, Blink);
};  // namespace FileSelectItemSub

FileSelectItem::FileSelectItem(s32 fileNo, bool isNewFile, const FileSelectIconID& rID, const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< FileSelectItem >);
    mIsNewFile = isNewFile;
    mPlanetMapObj = nullptr;
    mIconID = new FileSelectIconID(rID);
    mFaceParts = nullptr;
    mNumber = nullptr;
    mFileNo = fileNo;
    mIsPointed = 0;
    mIsInvalidateSelect = false;
    mIsMiiBroken = 0;
    mIsComplete = 0;
    mScaleCtrl = new FileSelectItemSub::ScaleController();
    mBlinkCtrl = new FileSelectItemSub::BlinkController(this);
    mDelegator = nullptr;
    mIsPointerOut = 1;
    mIsDragging = 0;
    mPrevPointerPos.x = 0.0f;
    mPrevPointerPos.y = 0.0f;
    mRotateSpeed = 0.0f;
    mIsInvalidRotate = true;
    mTurnTime = 0;
    mTurnStep = 0;
    mPlanetMtx.identity();
    mFellowMtx.identity();
    mFaceMtx.identity();

    mModels = new FileSelectModel*[::sFellowNum];

    for (s32 i = 0; i < ::sFellowNum; i++) {
        mModels[i] = nullptr;
    }
}

void FileSelectItem::init(const TableIter& /*rIter*/) {
    MR::connectToSceneMapObjMovement(this);
    MR::createSceneObj(SceneObj_MiiFacePartsHolder);
    createNew();
    createFellows();
    createMii();
    createNumber();
    MR::initStarPointerTarget(this, 1000.0f, Vec3f(0.0f, 900.0f, 0.0f));
    MR::invalidateClipping(this);
    initNerve(GET_NERVE_ANON(FileSelectItemNrvNewWait));
    MR::createCenterScreenBlur();
    makeActorAppeared();
}

void FileSelectItem::appear() {
    LiveActor::appear();

    if (mIsNewFile) {
        killAllModels();
        mPlanetMapObj->makeActorAppeared();
        setNerve(GET_NERVE_ANON(FileSelectItemNrvNewWait));
    } else {
        if (mIconID->isMii()) {
            killAllModels();
            mFaceParts->makeActorAppeared();
        } else {
            appearFellowModel();
        }

        setNerve(GET_NERVE_ANON(FileSelectItemNrvExistWait));
    }
}

void FileSelectItem::makeActorAppeared() {
    LiveActor::makeActorAppeared();
    mIsPointed = 0;
    mIsInvalidateSelect = false;
}

void FileSelectItem::makeActorDead() {
    LiveActor::makeActorDead();
    mPlanetMapObj->makeActorDead();

    for (s32 i = 0; i < ::sFellowNum; i++) {
        mModels[i]->makeActorDead();
    }

    mFaceParts->makeActorDead();
}

bool FileSelectItem::isNew() const {
    return isNerve(GET_NERVE_ANON(FileSelectItemNrvNewWait));
}

bool FileSelectItem::isExist() const {
    return isNerve(GET_NERVE_ANON(FileSelectItemNrvExistWait));
}

void FileSelectItem::format() {
    setNerve(GET_NERVE_ANON(FileSelectItemNrvFormat));
    deleteCompleteEffect();
    mIsNewFile = 1;
}

void FileSelectItem::change(const FileSelectIconID& rID, bool isComplete) {
    mIconID->set(rID);

    if (rID.isMii()) {
        setNerve(GET_NERVE_ANON(FileSelectItemNrvChangeMii));
    } else {
        setNerve(GET_NERVE_ANON(FileSelectItemNrvChangeFellow));
    }

    deleteCompleteEffect();
    mIsComplete = isComplete;
    mIsNewFile = 0;
}

void FileSelectItem::forceChange(const FileSelectIconID& rID, bool isComplete) {
    deleteCompleteEffect();
    mIconID->set(rID);
    if (rID.isMii()) {
        mFaceParts->changeFaceModel(MiiFaceRecipe(Mii::SourceLibrary, mIconID->getMiiIndex(), 256, Mii::expressionBit(Mii::ExpressionNormal) | Mii::expressionBit(Mii::ExpressionBlink)));
        killAllModels();
        mFaceParts->makeActorAppeared();
    } else {
        appearFellowModel();
    }

    mIsComplete = isComplete;
    emitCompleteEffect();
    setNerve(GET_NERVE_ANON(FileSelectItemNrvExistWait));
    mIsNewFile = 0;
}

void FileSelectItem::invalidateSelect() {
    if (mIsPointed) {
        offPointing();
    }

    mIsInvalidateSelect = true;
}

void FileSelectItem::validateSelect() {
    mIsInvalidateSelect = false;
}

void FileSelectItem::appearIndex() {
    mNumber->appear();
}

void FileSelectItem::disappearIndex() {
    mNumber->disappear();
}

void FileSelectItem::copyIconID(FileSelectIconID* pId) {
    pId->set(*mIconID);
}

void FileSelectItem::setSelectDelegator(FileSelectItemDelegatorBase* pDele) {
    mDelegator = pDele;
}

void FileSelectItem::onPointing() {
    if (!mIsInvalidateSelect) {
        if (isNerve(GET_NERVE_ANON(FileSelectItemNrvNewWait))) {
            playPointedNotUsingME();
        } else {
            playPointedME();
        }

        mNumber->onSelectIn();
        mIsPointed = 1;
        mScaleCtrl->setNerve(GET_NERVE_DIRECT(FileSelectItemSub, ScaleControllerNrvToBig));
        MR::tryRumblePadWeak(this, PLAYER_1);
    }
}

void FileSelectItem::offPointing() {
    if (!mIsInvalidateSelect) {
        mNumber->onSelectOut();
        mIsPointed = 0;
        mScaleCtrl->setNerve(GET_NERVE_DIRECT(FileSelectItemSub, ScaleControllerNrvToSmall));
    }
}

void FileSelectItem::validateRotate() {
    mIsInvalidRotate = false;
}

void FileSelectItem::turnToFront(s32 angle) {
    mTurnTime = angle;
    mTurnStep = 0;
}

void FileSelectItem::exeFormat() {

    if (MR::isLessStep(this, 40)) {
        MR::startSystemLevelSE("SE_SY_LV_FILE_SEL_MORPHBLUR");
    }

    if (MR::isStep(this, 40)) {
        MR::startSystemSE("SE_SY_FILE_SEL_MORPH_DUMMY");
        emitVanish();
        killAllModels();
        mPlanetMapObj->makeActorAppeared();
        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::shakeCameraNormal();
    }

    if (MR::isGreaterEqualStep(this, 40)) {
        mRotation.y = 0.0f;
        mRotateSpeed = 0.0f;
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        setNerve(GET_NERVE_ANON(FileSelectItemNrvNewWait));
    }
}

void FileSelectItem::exeChangeFellow() {

    if (MR::isLessStep(this, 40)) {
        MR::startSystemLevelSE("SE_SY_LV_FILE_SEL_MORPHBLUR");
    }

    if (MR::isStep(this, 40)) {
        MR::startSystemSE("SE_SY_FILE_SEL_MORPH_MARIO");
        appearFellowModel();
        emitCompleteEffect();
        emitOpen();

        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::shakeCameraNormal();
    }

    if (MR::isGreaterEqualStep(this, 40)) {
        mRotation.y = 0.0f;
        mRotateSpeed = 0.0f;
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(2500))) {
        setNerve(GET_NERVE_ANON(FileSelectItemNrvExistWait));
    }
}

void FileSelectItem::exeChangeMii() {

    if (MR::isLessStep(this, 40)) {
        MR::startSystemLevelSE("SE_SY_LV_FILE_SEL_MORPHBLUR");
    }

    if (MR::isStep(this, 39)) {
        mFaceParts->changeFaceModel(MiiFaceRecipe(Mii::SourceLibrary, mIconID->getMiiIndex(), 256, Mii::expressionBit(Mii::ExpressionNormal) | Mii::expressionBit(Mii::ExpressionBlink)));
    }

    if (MR::isStep(this, 40)) {
        MR::startSystemSE("SE_SY_FILE_SEL_MORPH_MARIO");
        killAllModels();
        mFaceParts->makeActorAppeared();
        emitCompleteEffect();
        emitOpen();

        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::shakeCameraNormal();
    }

    if (MR::isGreaterEqualStep(this, 40)) {
        mRotation.y = 0.0f;
        mRotateSpeed = 0.0f;
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(2500))) {
        setNerve(GET_NERVE_ANON(FileSelectItemNrvExistWait));
    }
}

void FileSelectItem::control() {
    updatePointing();
    updateRotate();

    Pos3f mtx;
    if (mRotation.x == 0.0f && mRotation.z == 0.0f) {
        MR::makeMtxTransRotateY(mtx, this);
    } else {
        MR::makeMtxTR(mtx, this);
    }

    Vec3f yDir;
    mtx.getYDir(yDir);

    Vec3f trans;
    mtx.getTrans(trans);
    mPlanetMtx.set(mtx);
    mPlanetMtx.setTrans(trans + yDir * 30.0f * 30.0f);

    mFellowMtx.set(mtx);
    mFaceMtx.set(mtx);

    mScaleCtrl->updateNerve();

    mPlanetMapObj->mScale.setAll< f32 >(30.0f * mScaleCtrl->getScale());

    for (s32 i = 0; i < ::sFellowNum; i++) {
        mModels[i]->mScale.setAll< f32 >(30.0f * mScaleCtrl->getScale());
    }

    mFaceParts->mScale.setAll< f32 >(27.0f * mScaleCtrl->getScale());

    mBlinkCtrl->updateNerve();

    Vec3f newPos;
    Vec3f screenPos;
    newPos.add(mPosition, ::sDataInfoOffset);
    MR::calcScreenPosition(&screenPos, newPos);
    mNumber->setTrans(screenPos);
}

void FileSelectItem::createNew() {
    mPlanetMapObj = MR::createPartsModelMapObj(this, "ニューフェイス", "FileSelectDataPlanet", mPlanetMtx);
    mPlanetMapObj->mScale.set(30.0f);
    mPlanetMapObj->makeActorDead();
}

void FileSelectItem::createFellows() {
    for (s32 i = 0; i < ::sFellowNum; i++) {
        mModels[i] = new FileSelectModel(::sFellowModel[i], mFellowMtx, "キャラフェイス");
    }
}

void FileSelectItem::createMii() {
    if (mIsNewFile || !mIconID->isMii()) {
        mFaceParts = MiiFacePartsHolder::createPartsFromDefault("Miiフェイス", 0);
    } else {
        mFaceParts = MiiFacePartsHolder::createPartsFromReceipe("Miiフェイス",
                                                                MiiFaceRecipe(Mii::SourceLibrary, mIconID->getMiiIndex(), 256, Mii::expressionBit(Mii::ExpressionNormal) | Mii::expressionBit(Mii::ExpressionBlink)));
    }

    mFaceParts->initFixedPosition(mFaceMtx, Vec3f(0.0f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f));
    mFaceParts->mScale.set(27.0f);
    mFaceParts->initEffectKeeper(0, "FileSelectDataMii");
    MR::invalidateClipping(mFaceParts);
    mFaceParts->makeActorDead();
}

void FileSelectItem::createNumber() {
    mNumber = new FileSelectNumber("ファイル番号");
    mNumber->initWithoutIter();
    mNumber->setNumber(mFileNo);
}

void FileSelectItem::updatePointing() {
    if (mIsInvalidateSelect) {
        if (MR::isStarPointerPointing1PWithoutCheckZ(this, nullptr, false, false)) {
            if (mDelegator != nullptr) {
                mDelegator->notify(this, 2);
            }
        }
    } else if (MR::isStarPointerPointingFileSelect(this)) {
        if (mDelegator != nullptr) {
            mDelegator->notify(this, 0);
        }
    }

    if (!mIsInvalidateSelect && mIsPointed && MR::testPointerMenuPadDecideTrigger()) {
        if (mDelegator != nullptr) {
            mDelegator->notify(this, 1);
            mScaleCtrl->setNerve(GET_NERVE_DIRECT(FileSelectItemSub, ScaleControllerNrvToSmall));
        }
    }
}

namespace {
    struct PointerSweepTriangle {
        Vec3f mVertexA;
        Vec3f mVertexB;
        Vec3f mVertexC;
        Vec3f mEdgeAB;
        Vec3f mEdgeBC;
        Vec3f mEdgeCA;
        Vec3f mNormal;

        void set(const Vec3f& rCameraPosition, const Vec3f& rCurrentPoint, const Vec3f& rPreviousPoint) {
            mVertexA = rCameraPosition;
            mVertexB = rCurrentPoint;
            mVertexC = rPreviousPoint;

            mEdgeAB = rCurrentPoint - rCameraPosition;
            mEdgeBC = rPreviousPoint - rCurrentPoint;
            mEdgeCA = rCameraPosition - rPreviousPoint;
            mNormal.cross(mEdgeBC, mEdgeAB);
            MR::normalize(&mNormal);
        }

        PointerSweepTriangle(const Vec3f& rA, const Vec3f& rB, const Vec3f& rC) {
            set(rA, rB, rC);
        }

        bool contains(const Vec3f& rPoint) const {
            bool containsPoint;
            Vec3f cross;
            cross.cross(rPoint - mVertexA, mEdgeAB);

            if (cross.dot(mNormal) < 0.0f) {
                containsPoint = false;
            } else {
                cross.cross(rPoint - mVertexB, mEdgeBC);

                if (cross.dot(mNormal) < 0.0f) {
                    containsPoint = false;
                } else {
                    cross.cross(rPoint - mVertexC, mEdgeCA);

                    containsPoint = !(cross.dot(mNormal) < 0.0f);
                }
            }

            return containsPoint;
        }

        void project(Vec3f* pResult, const Vec3f& rCenter, f32 distance) const {
            Vec3f offset(mNormal);
            offset.scale(distance);
            *pResult = rCenter - offset;
        }
    };

    inline bool checkCollisionOfPointAndTriangle(const Vec3f& rCenter, const Vec3f& rCameraPosition, const Vec3f& rCurrentPoint,
                                                 const Vec3f& rPreviousPoint) {
        PointerSweepTriangle triangle(rCameraPosition, rCurrentPoint, rPreviousPoint);

        Vec3f delta(rCenter - rCameraPosition);
        f32 planeDistance = triangle.mNormal.dot(delta);

        bool containsPoint;
        bool collides;

        if (MR::abs(planeDistance) >= 900.0f) {
            collides = false;
        } else {
            Vec3f projected;
            triangle.project(&projected, rCenter, planeDistance);
            containsPoint = triangle.contains(projected);
            if (containsPoint) {
                collides = true;
            } else if (triangle.mVertexA.distance(rCenter) <= 900.0f) {
                collides = true;
            } else if (triangle.mVertexB.distance(rCenter) <= 900.0f) {
                collides = true;
            } else if (triangle.mVertexC.distance(rCenter) <= 900.0f) {
                collides = true;
            } else if (::checkCollisionOfPointAndCylinder(rCenter, triangle.mVertexA, triangle.mEdgeAB, 900.0f)) {
                collides = true;
            } else if (::checkCollisionOfPointAndCylinder(rCenter, triangle.mVertexB, triangle.mEdgeBC, 900.0f)) {
                collides = true;
            } else {
                collides = ::checkCollisionOfPointAndCylinder(rCenter, triangle.mVertexC, triangle.mEdgeCA, 900.0f);
            }
        }

        return collides;
    }

};  // namespace

void FileSelectItem::updateRotate() {
    if (mIsInvalidRotate) {
        return;
    }

    const s32 duration = mTurnTime;
    if (duration > 0) {
        mRotateSpeed = 0.0f;

        mTurnStep++;
        const s32 step = mTurnStep;
        f32 rate = static_cast< f32 >(step) / duration;
        rate *= rate;
        if (rate > 1.0f) {
            rate = 1.0f;
        }

        if (rate < 0.0f) {
            rate = 0.0f;
        }

        if (step >= duration) {
            mTurnStep = 0;
            mTurnTime = 0;
        }

        f32 angle = MR::repeat(mRotation.y, -180.0f, 360.0f);
        mRotation.y = angle - angle * rate;
        FileSelectItemSub::BlinkController* pBlink = mBlinkCtrl;
        pBlink->open();
        pBlink->setNerve(GET_NERVE_DIRECT(FileSelectItemSub, BlinkControllerNrvOpen));
        return;
    } else if (MR::isStarPointerInScreen(0)) {
        Vec3f center = mPosition + Vec3f(0.0f, 900.0f, 0.0f);
        Vec2f screenPos(*MR::getStarPointerScreenPosition(0));

        if (mIsPointerOut) {
            mPrevPointerPos = screenPos;
            mIsPointerOut = 0;
        }

        Vec3f cameraPosition = MR::getCamPos();
        Vec3f cameraToCenter = center - cameraPosition;

        f32 pointerDistance = (900.0f + cameraToCenter.length());
        if (FloatUtil::sqrt(screenPos.squareDist(mPrevPointerPos)) < 2.0f) {
            Vec3f pointerPosition;
            MR::calcWorldPositionFromScreen(&pointerPosition, screenPos, pointerDistance);
            Vec3f pointerDirection = pointerPosition - cameraPosition;
            MR::normalize(&pointerDirection);
            Vec3f crossDirection = cameraToCenter.cross(pointerDirection);

            if (crossDirection.length() < 900.0f) {
                mIsDragging = 1;
            }
        } else {
            Vec3f currentPoint;
            MR::calcWorldPositionFromScreen(&currentPoint, screenPos, pointerDistance);
            Vec3f previousPoint;
            MR::calcWorldPositionFromScreen(&previousPoint, mPrevPointerPos, pointerDistance);
            bool collides = ::checkCollisionOfPointAndTriangle(center, cameraPosition, currentPoint, previousPoint);

            if (collides) {
                mIsDragging = 1;
            }
        }

        if (mIsDragging) {
            Vec2f delta = screenPos - mPrevPointerPos;
            mRotateSpeed += 0.03f * delta.x / mScale.x;
            mRotateSpeed = MR::clamp(mRotateSpeed, -25.0f, 25.0f);
        }

        mPrevPointerPos = screenPos;
    } else {
        mIsPointerOut = 1;
    }

    mRotateSpeed *= 0.98f;

    if (mRotateSpeed >= 0.0f && mRotateSpeed < 0.5f) {
        mRotateSpeed = 0.5f;
    }

    if (mRotateSpeed < 0.0f && mRotateSpeed > -0.5f) {
        mRotateSpeed = -0.5f;
    }

    mRotation.y = MR::repeat(mRotation.y + mRotateSpeed, 0.0f, 360.0f);
    mIsDragging = 0;
}

void FileSelectItem::playPointedME() {
    switch (MR::getRandom(0, 5)) {
    case 0:
        MR::startSystemME("ME_ASTRO_DOME_HIT_GALAXY1");

        break;
    case 1:
        MR::startSystemME("ME_ASTRO_DOME_HIT_GALAXY2");

        break;
    case 2:
        MR::startSystemME("ME_ASTRO_DOME_HIT_GALAXY3");

        break;
    case 3:
        MR::startSystemME("ME_ASTRO_DOME_HIT_GALAXY4");

        break;
    case 4:
        MR::startSystemME("ME_ASTRO_DOME_HIT_GALAXY5");

        break;
    }
}

void FileSelectItem::playPointedNotUsingME() {
    switch (MR::getRandom(0, 5)) {
    case 0:
        MR::startSystemME("ME_ASTRO_DOME_HIT_GALAXY_N1");

        break;
    case 1:
        MR::startSystemME("ME_ASTRO_DOME_HIT_GALAXY_N2");

        break;
    case 2:
        MR::startSystemME("ME_ASTRO_DOME_HIT_GALAXY_N3");

        break;
    case 3:
        MR::startSystemME("ME_ASTRO_DOME_HIT_GALAXY_N4");

        break;
    case 4:
        MR::startSystemME("ME_ASTRO_DOME_HIT_GALAXY_N5");

        break;
    }
}

void FileSelectItem::appearFellowModel() {
    killAllModels();
    mModels[mIconID->getFellowID()]->makeActorAppeared();
}

void FileSelectItem::killAllModels() {
    mPlanetMapObj->makeActorDead();

    for (s32 i = 0; i < ::sFellowNum; i++) {
        mModels[i]->makeActorDead();
    }

    mFaceParts->makeActorDead();
}

void FileSelectItem::emitOpen() {
    if (!MR::isDead(mPlanetMapObj)) {
        MR::emitEffect(mPlanetMapObj, "Open");
    }

    for (s32 i = 0; i < ::sFellowNum; i++) {
        if (!MR::isDead(mModels[i])) {
            mModels[i]->emitOpen();
        }
    }

    if (!MR::isDead(mFaceParts)) {
        MR::emitEffect(mFaceParts, "Open");
    }
}

void FileSelectItem::emitVanish() {
    if (!MR::isDead(mPlanetMapObj)) {
        MR::emitEffect(mPlanetMapObj, "Vanish");
    }

    for (s32 i = 0; i < ::sFellowNum; i++) {
        if (!MR::isDead(mModels[i])) {
            mModels[i]->emitVanish();
        }
    }

    if (!MR::isDead(mFaceParts)) {
        MR::emitEffect(mFaceParts, "Vanish");
    }
}

void FileSelectItem::emitCompleteEffect() {
    if (!mIsComplete) {
        return;
    }

    for (s32 i = 0; i < ::sFellowNum; i++) {
        if (!MR::isDead(mModels[i])) {
            mModels[i]->emitCompleteEffect();
        }
    }

    if (!MR::isDead(mFaceParts)) {
        MR::emitEffect(mFaceParts, "Complete");
    }
}

void FileSelectItem::deleteCompleteEffect() {
    for (s32 i = 0; i < ::sFellowNum; i++) {
        mModels[i]->deleteCompleteEffect();
    }

    MR::deleteEffect(mFaceParts, "Complete");
}

namespace FileSelectItemSub {

    ScaleController::ScaleController() : NerveExecutor("ファイルセレクタアイコンサイズ管理") {
        mScale = 1.0f;
        initNerve(GET_NERVE_DIRECT(FileSelectItemSub, ScaleControllerNrvSmall));
    }

    void ScaleController::exeToSmall() {
        if (MR::isLessEqualStep(this, cScaleEaseSteps)) {
            f32 rate = (static_cast< f32 >(getNerveStep()) / static_cast< f32 >(cScaleEaseSteps));
            mScale += (rate * (1.0f - mScale));
        }

        MR::setNerveAtStep(this, GET_NERVE_DIRECT(FileSelectItemSub, ScaleControllerNrvSmall), cScaleEaseSteps);
    }

    void ScaleController::exeToBig() {
        if (MR::isLessEqualStep(this, cScaleEaseSteps)) {
            f32 rate = (static_cast< f32 >(getNerveStep()) / static_cast< f32 >(cScaleEaseSteps));
            mScale += rate * (1.2f - mScale);
        }

        MR::setNerveAtStep(this, GET_NERVE_DIRECT(FileSelectItemSub, ScaleControllerNrvBig), cScaleEaseSteps);
    }

    BlinkController::BlinkController(FileSelectItem* pItem) : NerveExecutor("ファイルセレクタアイコン瞬き管理") {
        mItem = pItem;
        mWaitStep = 0;
        mBlinkStep = 0;
        initNerve(GET_NERVE_DIRECT(FileSelectItemSub, BlinkControllerNrvOpen));
    }

    void BlinkController::exeOpen() {
        if (MR::isFirstStep(this)) {
            mWaitStep = MR::getRandom(180, 300);
            mBlinkStep = 0;
        }

        // 1.5-3 s of spinning puts the icon to sleep.
        f32 spinSpeed = MR::abs(mItem->mRotateSpeed);

        if (spinSpeed > 10.0f) {
            mBlinkStep += 2;
        } else {
            if (spinSpeed > 6.0f) {
                mBlinkStep++;
            } else {
                mBlinkStep = 0;
            }
        }

        if (mBlinkStep > NebulaFrameClock::stepsOfSeconds(3)) {
            setNerve(GET_NERVE_DIRECT(FileSelectItemSub, BlinkControllerNrvSleep));
        } else {
            if (MR::isGreaterEqualStep(this, mWaitStep)) {
                setNerve(GET_NERVE_DIRECT(FileSelectItemSub, BlinkControllerNrvShut));
            }
        }
    }

    void BlinkController::exeShut() {
        if (MR::isFirstStep(this)) {
            shut();
        }

        if (MR::isGreaterEqualStep(this, 10)) {
            open();
            setNerve(GET_NERVE_DIRECT(FileSelectItemSub, BlinkControllerNrvOpen));
        }
    }

    void BlinkController::exeSleep() {
        if (MR::isFirstStep(this)) {
            sleep();
            mBlinkStep = 0;
        }

        mBlinkStep++;

        if (MR::abs(mItem->mRotateSpeed) > 2.0f) {
            mBlinkStep = 0;
        }

        if (mBlinkStep > NebulaFrameClock::stepsOfSeconds(1)) {
            setNerve(GET_NERVE_DIRECT(FileSelectItemSub, BlinkControllerNrvBlink));
        }
    }

    void BlinkController::exeBlink() {
        if (MR::isFirstStep(this) && mItem->mIconID->isFellow()) {
            mItem->mModels[mItem->mIconID->getFellowID()]->blink();
        }

        if (getNerveStep() % 8 < 4) {
            mItem->mFaceParts->changeExpressionBlink();

        } else {
            mItem->mFaceParts->changeExpressionNormal();
        }

        if (mItem->mIconID->isFellow()) {
            FileSelectIconID::EFellowID id = mItem->mIconID->getFellowID();

            if (mItem->mModels[id]->isOpen()) {
                setNerve(GET_NERVE_DIRECT(FileSelectItemSub, BlinkControllerNrvOpen));
                return;
            }
        }

        if (mItem->mIconID->isMii()) {
            if (MR::isGreaterEqualStep(this, 40)) {
                mItem->mFaceParts->changeExpressionNormal();
                setNerve(GET_NERVE_DIRECT(FileSelectItemSub, BlinkControllerNrvOpen));
            }
        }
    }

    void BlinkController::shut() {
        if (!mItem->mIsNewFile) {
            if (mItem->mIconID->isMii()) {
                mItem->mFaceParts->changeExpressionBlink();
            } else {
                mItem->mModels[mItem->mIconID->getFellowID()]->blinkOnce();
            }
        }
    }

    void BlinkController::open() {
        if (!mItem->mIsNewFile) {
            if (mItem->mIconID->isMii()) {
                mItem->mFaceParts->changeExpressionNormal();
            } else {
                mItem->mModels[mItem->mIconID->getFellowID()]->open();
            }
        }
    }

    void BlinkController::sleep() {
        if (!mItem->mIsNewFile) {
            if (mItem->mIconID->isMii()) {
                mItem->mFaceParts->changeExpressionBlink();
            } else {
                mItem->mModels[mItem->mIconID->getFellowID()]->close();
            }
        }
    }

    void ScaleController::exeBig() {
        mScale = 1.2f;
    }

    void ScaleController::exeSmall() {
        mScale = 1.0f;
    }
};  // namespace FileSelectItemSub

void FileSelectItem::exeExistWait() {
}

void FileSelectItem::exeNewWait() {
}

FileSelectItem::~FileSelectItem() {
}
