#include "Game/MapObj/BigFan.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/BigFanHolder.hpp"
#include "Game/Util.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initWindModel_f1, "initWindModel.f1", 2000.0f);

namespace {
    NEBULA_TUNABLE(f32, sBaseWindWidth, 400.0f);
};  // namespace

namespace NrvBigFan {
    NEW_NERVE(BigFanNrvStop, BigFan, Stop);
    NEW_NERVE(BigFanNrvStart, BigFan, Start);
    NEW_NERVE(BigFanNrvWait, BigFan, Wait);
};  // namespace NrvBigFan

BigFan::BigFan(const char* pName)
    : LiveActor(pName), mWindModel(), mClippingCenter(0.0f, 0.0f, 0.0f), mWindLength(4000.0f), mWindSpeed(100.0f), mIsTeresaGalaxy() {
    BigFanFunction::createBigFanHolder();
    BigFanFunction::registerBigFan(this);
}

void BigFan::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    const char* object_name;
    MR::getObjectName(&object_name, rIter);
    initModelManagerWithAnm(object_name, 0, false);
    MR::connectToSceneNoSilhouettedMapObj(this);

    if (MR::isExistCollisionResource(this, object_name)) {
        initHitSensor(1);
        MR::addBodyMessageSensorReceiver(this);
        MR::initCollisionParts(this, object_name, getSensor(nullptr), nullptr);
    }

    MR::getPlacementArg0NoInit(rIter, &mWindLength);
    MR::getPlacementArg1NoInit(rIter, &mWindSpeed);
    initWindModel();
    Vec3f front;
    MR::calcFrontVec(&front, this);
    mClippingCenter.scaleAdd(mWindLength * cNum_init_f1, front, mPosition);
    MR::setClippingTypeSphere(this, cNum_init_f2 + mWindLength, &mClippingCenter);
    initSound(4, false);

    if (MR::isEqualStageName("TeresaMario2DGalaxy")) {
        mIsTeresaGalaxy = true;
    } else {
        mIsTeresaGalaxy = false;
    }

    initNerve(GET_NERVE(BigFan, BigFanNrvWait));
    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::listenStageSwitchOnAppear(this, MR::Functor(this, &BigFan::start));
        setNerve(GET_NERVE(BigFan, BigFanNrvStop));
        mWindModel->kill();
    }

    MR::registerDemoSimpleCastAll(this);
    makeActorAppeared();
}

void BigFan::initWindModel() {
    mWindModel = MR::createModelObjMapObj("風モデル", "BigFanWind", getBaseMtx());
    mWindModel->initWithoutIter();
    MR::invalidateClipping(mWindModel);
    MR::startBtk(mWindModel, "BigFanWind");
    MR::registerDemoSimpleCastAll(mWindModel);
    mWindModel->mScale.z = mWindLength / cNum_initWindModel_f1;
}

void BigFan::calcWindInfo(Vec3f* pWindInfo, const Vec3f& rPos) {
    if (MR::isDead(this) || !isStartOrWait()) {
        pWindInfo->zero();
        return;
    }

    if (mWindLength <= 0.0f) {
        pWindInfo->zero();
        return;
    }

    Vec3f front;
    MR::calcFrontVec(&front, this);
    MR::normalize(&front);
    Vec3f offset = rPos - mPosition;
    f32 dot = front.dot(offset);

    if (dot < 0.0f) {
        pWindInfo->zero();
        return;
    }

    Vec3f ortho;
    ortho = offset - front * dot;
    f32 mag = ortho.length();

    if (mag >= ::sBaseWindWidth * mScale.x) {
        pWindInfo->zero();
        return;
    }

    f32 scalar = 1.0f - (dot / mWindLength);
    if (scalar < 0.0f) {
        pWindInfo->zero();
        return;
    }

    pWindInfo->set(front * scalar);
    return;
}

void BigFan::control() {
}

void BigFan::start() {
    if (isNerve(GET_NERVE(BigFan, BigFanNrvStop))) {
        setNerve(GET_NERVE(BigFan, BigFanNrvStart));
    }
}

void BigFan::exeStop() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Appear");
        MR::stopBck(this);
    }
}

void BigFan::exeStart() {
    if (MR::isFirstStep(this)) {
        mWindModel->appear();
        MR::startAction(this, "Appear");
        MR::startAction(mWindModel, "Appear");

        if (mIsTeresaGalaxy) {
            MR::startSound(this, "SE_OJ_BIG_FAN_START_FAR");
        } else {
            MR::startSound(this, "SE_OJ_BIG_FAN_START");
        }
    }

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(BigFan, BigFanNrvWait));
    }
}

void BigFan::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Wait");
        MR::startAction(mWindModel, "Wait");
    }

    if (mIsTeresaGalaxy) {
        MR::startLevelSound(this, "SE_OJ_LV_BIG_FAN_FAR");
    } else {
        MR::startLevelSound(this, "SE_OJ_LV_BIG_FAN");
    }
}

bool BigFan::isStartOrWait() {
    return isNerve(GET_NERVE(BigFan, BigFanNrvStart)) || isNerve(GET_NERVE(BigFan, BigFanNrvWait));
}
