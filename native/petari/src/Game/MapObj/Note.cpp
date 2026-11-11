#include "Game/MapObj/Note.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/NoteFairy.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/NoteCounter.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 30.0f);

namespace {
    NEBULA_TUNABLE(f32, sRotateSpeed, 8.0f);
    NEBULA_TUNABLE(s32, sStepFlyUp, 10);
    NEBULA_TUNABLE(f32, sFlyUpSpeed, 30.0f);
};  // namespace

namespace NrvNote {
    NEW_NERVE(NoteNrvWait, Note, Wait);
    NEW_NERVE(NoteNrvCountDown, Note, CountDown);
    NEW_NERVE(NoteNrvFlyUp, Note, FlyUp);
};  // namespace NrvNote

Note::Note(const char* pName, const Vec3f& rRailDirection, NoteFairy* pParent) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< Note >);
    mParentFairy = pParent;
    mRailDirection = rRailDirection;
    mCounter = 0;
    mBrkFrame = 0.0f;
    mIsCountdown = false;
    mFlashCtrl = nullptr;
}

void Note::init(const TableIter& /*rIter*/) {
    MR::createSceneObj(SceneObj_NoteGroup);
    initModelManagerWithAnm("Note", nullptr, false);
    MR::connectToSceneNoSilhouettedMapObjStrongLight(this);
    initHitSensor(1);
    MR::addHitSensorMapObj(this, "body", 8, cNum_init_f1, Vec3f(0.0f, 0.0f, 0.0f));
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    initNerve(GET_NERVE(Note, NoteNrvWait));
    mFlashCtrl = new FlashingCtrl(this, true);
    MR::calcGravityAndDropShadowVector(this, &mGravity, nullptr, 0);

    if (MR::isNearZero(mGravity)) {
        mGravity.set(0.0f, -1.0f, 0.0);
    }

    MR::initShadowVolumeSphere(this, cNum_init_f2);
    MR::turnVecToPlane(&mRailDirection, mRailDirection, mGravity);
    MR::invalidateClipping(this);
    makeActorDead();
}

void Note::startCountDown() {
    mIsCountdown = true;
}

void Note::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBrk(this, "Note");
        f32 framemax = MR::getBrkFrameMax(this, "Note");
        MR::setBrkFrame(this, MR::repeat(mBrkFrame, 0.0f, framemax));
    }

    if (mIsCountdown) {
        setNerve(GET_NERVE(Note, NoteNrvCountDown));
    }
}

void Note::exeCountDown() {
    if (MR::isFirstStep(this)) {
        mFlashCtrl->start(mCounter);
    }

    if (MR::isStep(this, mCounter)) {
        MR::emitEffect(this, "Vanish");
        MR::startSound(this, "SE_OJ_MUSICAL_NOTE_BREAK");
        MR::failureNoteCollection();
        kill();
    }
}

void Note::exeFlyUp() {
    if (MR::isFirstStep(this)) {
        mVelocity.set(mGravity);
        mVelocity *= -::sFlyUpSpeed;
        MR::invalidateHitSensors(this);
        mFlashCtrl->end();
    }

    if (MR::isStep(this, ::sStepFlyUp)) {
        kill();
    }
}

void Note::control() {
    mRotation.y = MR::getSceneObj< NoteGroup >(SceneObj_NoteGroup)->mRotation;
}

void Note::calcAndSetBaseMtx() {
    Pos3f baseMtx;
    MR::makeMtxUpFront(&baseMtx, -mGravity, mRailDirection);
    Mtx mtxRotateY;
    f32 groupRotation = MR::getSceneObj< NoteGroup >(SceneObj_NoteGroup)->mRotation;
    MR::makeMtxRotateY(mtxRotateY, groupRotation);
    MR::multMtx(baseMtx, mtxRotateY, baseMtx);
    baseMtx.setTrans(mPosition);
    MR::setBaseTRMtx(this, baseMtx);
}

void Note::attackSensor(HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (MR::isSensorPlayerOrRide(pReceiver)) {
        if (!isNerve(GET_NERVE(Note, NoteNrvFlyUp))) {
            MR::emitEffect(this, "Get");
            MR::addNoteToCounter();
            mParentFairy->setLastGotNote(this);
            setNerve(GET_NERVE(Note, NoteNrvFlyUp));
        }
    }
}

NoteGroup::NoteGroup() : LiveActorGroup("音符グループ", 0x100) {
    lumacore::setClass(this, lumacore::kClassId< NoteGroup >);
    mRotation = 0.0f;
}

void NoteGroup::init(const TableIter& /*rIter*/) {
    MR::connectToSceneMapObjMovement(this);
}

void NoteGroup::movement() {
    mRotation += ::sRotateSpeed;
    MR::repeatDegree(&mRotation);
}
