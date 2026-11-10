#include "Game/Map/KoopaBattleMapPlanet.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/Map/KoopaBattleMapCoinPlate.hpp"
#include "Game/Map/KoopaBattleMapDamagePlate.hpp"
#include "Game/Map/KoopaBattleMapPlate.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createPlate_f1, "createPlate.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createPlate_f2, "createPlate.f2", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createPlate_f3, "createPlate.f3", 270.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createPlate_f4, "createPlate.f4", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createPlate_f5, "createPlate.f5", 270.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createPlate_f6, "createPlate.f6", 1180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createPlate_f7, "createPlate.f7", 55.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createPlate_f8, "createPlate.f8", 45.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createPlate_f9, "createPlate.f9", 125.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createPlate_f10, "createPlate.f10", 135.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createPlate_f11, "createPlate.f11", 55.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createPlate_f12, "createPlate.f12", 225.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createPlate_f13, "createPlate.f13", 125.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createPlate_f14, "createPlate.f14", 315.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createPlate_f15, "createPlate.f15", 1200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createPlate_f16, "createPlate.f16", 125.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createPlate_f17, "createPlate.f17", 45.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createPlate_f18, "createPlate.f18", 55.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createPlate_f19, "createPlate.f19", 135.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createPlate_f20, "createPlate.f20", 125.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createPlate_f21, "createPlate.f21", 225.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createPlate_f22, "createPlate.f22", 55.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createPlate_f23, "createPlate.f23", 315.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createPlate_f24, "createPlate.f24", 1230.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeDemoGrandStarAppear_1, "exeDemoGrandStarAppear.1", 151);

namespace {
    NEBULA_TUNABLE(f32, sRotateFriction, 0.995f);
};  // namespace

namespace NrvKoopaBattleMapPlanet {
    NEW_NERVE(KoopaBattleMapPlanetNrvWait, KoopaBattleMapPlanet, Wait);
    NEW_NERVE(KoopaBattleMapPlanetNrvDemoGrandStarAppear, KoopaBattleMapPlanet, DemoGrandStarAppear);
};  // namespace NrvKoopaBattleMapPlanet

KoopaBattleMapPlanet::KoopaBattleMapPlanet(const char* pName, const char* pModelName, bool isFinalPlanet, bool isNoPlate)
    : LiveActor(pName), mModelName(pModelName), mRotateMoment(0.0f, 0.0f, 0.0f), mDamagePlateGroup(), mCoinPlateGroup(), mPlateGroup(), mDamagePlate(),
      mIsFinalPlanet(isFinalPlanet), mIsNoPlate(isNoPlate) {
    lumacore::setClass(this, lumacore::kClassId< KoopaBattleMapPlanet >);
    mBaseMtx.identity();
}

void KoopaBattleMapPlanet::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    MR::makeMtxTR(mBaseMtx, this);

    initModelManagerWithAnm(mModelName, nullptr, false);
    MR::tryStartAllAnim(this, mModelName);

    MR::connectToSceneMapObj(this);

    initHitSensor(1);
    MR::initCollisionParts(this, mModelName, MR::addBodyMessageSensorMapObj(this), nullptr);
    initEffectKeeper(0, nullptr);

    f32 boundingRadius = 0.0f;
    MR::calcModelBoundingRadius(&boundingRadius, this);
    MR::setClippingTypeSphere(this, cNum_init_f1 + boundingRadius);
    MR::setClippingFarMax(this);

    initNerve(GET_NERVE(KoopaBattleMapPlanet, KoopaBattleMapPlanetNrvWait));

    if (!mIsNoPlate) {
        createPlate();
    }

    MR::registerDemoSimpleCastAll(this);

    if (!mIsFinalPlanet && !mIsNoPlate) {
        MR::registerDemoSimpleCastAll(MR::createBloomModel(this, nullptr));
    }

    makeActorAppeared();
}

void KoopaBattleMapPlanet::startAnimPowerStarAppear() {
    setNerve(GET_NERVE(KoopaBattleMapPlanet, KoopaBattleMapPlanetNrvDemoGrandStarAppear));
}

void KoopaBattleMapPlanet::exeWait() {
    if (!MR::isFirstStep(this)) {
        return;
    }

    MR::startBrk(this, "Wait");
}

void KoopaBattleMapPlanet::exeDemoGrandStarAppear() {
    if ((!mIsFinalPlanet && MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) || (mIsFinalPlanet && MR::isStep(this, cStep_exeDemoGrandStarAppear_1))) {
        MR::startBrk(mDamagePlate, "PowerStarAppear");
        MR::emitEffect(mDamagePlate, "GrandStarAppear");
    }
}

void KoopaBattleMapPlanet::createPlate() {
    mDamagePlateGroup = new LiveActorGroup("ダメージプレートＧ", 6);
    mDamagePlateGroup->initWithoutIter();

    for (s32 idx = 0; idx < 6; idx++) {
        f32 angles[6][2] = {{0.0f, 0.0f}, {cNum_createPlate_f1, 0.0f}, {cNum_createPlate_f2, 0.0f}, {cNum_createPlate_f3, 0.0f}, {0.0f, cNum_createPlate_f4}, {0.0f, cNum_createPlate_f5}};
        Vec3f vec(0.0f, cNum_createPlate_f6, 0.0f);
        MR::rotateVecDegree(&vec, Vec3f(0.0f, 0.0f, 1.0f), angles[idx][0]);
        MR::rotateVecDegree(&vec, Vec3f(1.0f, 0.0f, 0.0f), angles[idx][1]);

        mBaseMtx.mult(vec, vec);

        KoopaBattleMapDamagePlate* pDamagePlate = new KoopaBattleMapDamagePlate("クッパダメージプレート");
        pDamagePlate->mPosition.set(vec);
        pDamagePlate->initWithoutIter();

        mDamagePlateGroup->registerActor(pDamagePlate);
    }

    if (mIsFinalPlanet) {
        mDamagePlate = mDamagePlateGroup->getActor(0);
    } else {
        mDamagePlate = mDamagePlateGroup->getActor(1);
    }

    if (mIsFinalPlanet) {
        return;
    }

    mCoinPlateGroup = new LiveActorGroup("コインプレートＧ", 4);
    mCoinPlateGroup->initWithoutIter();

    for (s32 idx = 0; idx < 4; idx++) {
        f32 angles[4][2] = {{cNum_createPlate_f7, cNum_createPlate_f8}, {cNum_createPlate_f9, cNum_createPlate_f10}, {cNum_createPlate_f11, cNum_createPlate_f12}, {cNum_createPlate_f13, cNum_createPlate_f14}};
        Vec3f vec(0.0f, cNum_createPlate_f15, 0.0f);
        MR::rotateVecDegree(&vec, Vec3f(0.0f, 0.0f, 1.0f), angles[idx][0]);
        MR::rotateVecDegree(&vec, Vec3f(0.0f, 1.0f, 0.0f), angles[idx][1]);

        mBaseMtx.mult(vec, vec);

        KoopaBattleMapCoinPlate* pCoinPlate = new KoopaBattleMapCoinPlate("クッパコインプレート");
        pCoinPlate->mPosition.set(vec);
        pCoinPlate->initWithoutIter();

        mCoinPlateGroup->registerActor(pCoinPlate);
    }

    mPlateGroup = new LiveActorGroup("プレートＧ", 4);
    mPlateGroup->initWithoutIter();

    for (s32 idx = 0; idx < 4; idx++) {
        f32 angles[4][2] = {{cNum_createPlate_f16, cNum_createPlate_f17}, {cNum_createPlate_f18, cNum_createPlate_f19}, {cNum_createPlate_f20, cNum_createPlate_f21}, {cNum_createPlate_f22, cNum_createPlate_f23}};
        Vec3f vec(0.0f, cNum_createPlate_f24, 0.0f);
        MR::rotateVecDegree(&vec, Vec3f(0.0f, 0.0f, 1.0f), angles[idx][0]);
        MR::rotateVecDegree(&vec, Vec3f(0.0f, 1.0f, 0.0f), angles[idx][1]);

        mBaseMtx.mult(vec, vec);

        KoopaBattleMapPlate* pCoinPlate = new KoopaBattleMapPlate("クッパプレート");
        pCoinPlate->mPosition.set(vec);
        pCoinPlate->initWithoutIter();

        mPlateGroup->registerActor(pCoinPlate);
    }
}

void KoopaBattleMapPlanet::control() {
    MR::rotateMtxMoment(&mBaseMtx, mRotateMoment);
    mRotateMoment *= ::sRotateFriction;
}

void KoopaBattleMapPlanet::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mBaseMtx);
}
