#include "Game/Boss/TombSpiderParts.hpp"
#include "Game/Boss/TombSpider.hpp"
#include "Game/Boss/TombSpiderAcid.hpp"
#include "Game/Boss/TombSpiderGland.hpp"
#include "Game/Boss/TombSpiderThreadAttacher.hpp"
#include "Game/Boss/TombSpiderVitalSpot.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f1, "initParts.f1", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f2, "initParts.f2", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f3, "initParts.f3", 35.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f4, "initParts.f4", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f5, "initParts.f5", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f6, "initParts.f6", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f7, "initParts.f7", 35.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f8, "initParts.f8", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f9, "initParts.f9", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f10, "initParts.f10", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f11, "initParts.f11", 33.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f12, "initParts.f12", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f13, "initParts.f13", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f14, "initParts.f14", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f15, "initParts.f15", 33.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f16, "initParts.f16", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f17, "initParts.f17", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f18, "initParts.f18", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f19, "initParts.f19", 130.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f20, "initParts.f20", 280.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f21, "initParts.f21", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f22, "initParts.f22", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f23, "initParts.f23", 167.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f24, "initParts.f24", 130.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f25, "initParts.f25", 280.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f26, "initParts.f26", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f27, "initParts.f27", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParts_f28, "initParts.f28", 167.0f);

namespace {
    NEBULA_TUNABLE(f32, sAttachBodyBackRadius, 500.0f);
    NEBULA_TUNABLE(f32, sAttachBodyBackOffset, 100.0f);
    NEBULA_TUNABLE(f32, sAttachFootRadius, 300.0f);
    NEBULA_TUNABLE(f32, sAttachFootOffset, 100.0f);
};  // namespace

TombSpiderParts::TombSpiderParts(TombSpider* pParent)
    : mParent(pParent), mGlandFrontL(nullptr), mGlandFrontR(nullptr), mGlandRearL(nullptr), mGlandRearR(nullptr), mVitalSpotC(nullptr),
      mVitalSpotL(nullptr), mVitalSpotR(nullptr), mAcid(nullptr), mThreadAttacherWing(nullptr), mThreadAttacherFrontL(nullptr),
      mThreadAttacherCenterL(nullptr), mThreadAttacherRearL(nullptr), mThreadAttacherFrontR(nullptr), mThreadAttacherCenterR(nullptr),
      mThreadAttacherRearR(nullptr), mMtxEye(nullptr), mMtxMouth(nullptr), mMtxHip(nullptr), mMtxGlandFrontL(nullptr), mMtxGlandFrontR(nullptr),
      mMtxGlandRearL(nullptr), mMtxGlandRearR(nullptr) {
}

void TombSpiderParts::appearThreadAttacherAll() {
    mThreadAttacherWing->appear();
    mThreadAttacherFrontL->appear();
    mThreadAttacherCenterL->appear();
    mThreadAttacherRearL->appear();
    mThreadAttacherFrontR->appear();
    mThreadAttacherCenterR->appear();
    mThreadAttacherRearR->appear();
}

void TombSpiderParts::killThreadAttacherAll() {
    mThreadAttacherWing->kill();
    mThreadAttacherFrontL->kill();
    mThreadAttacherCenterL->kill();
    mThreadAttacherRearL->kill();
    mThreadAttacherFrontR->kill();
    mThreadAttacherCenterR->kill();
    mThreadAttacherRearR->kill();
}

void TombSpiderParts::pauseOffThreadAttacherAll() {
    MR::requestMovementOn(mThreadAttacherWing);
    MR::requestMovementOn(mThreadAttacherFrontL);
    MR::requestMovementOn(mThreadAttacherCenterL);
    MR::requestMovementOn(mThreadAttacherRearL);
    MR::requestMovementOn(mThreadAttacherFrontR);
    MR::requestMovementOn(mThreadAttacherCenterR);
    MR::requestMovementOn(mThreadAttacherRearR);
}

void TombSpiderParts::init() {
    MR::startBrk(mParent, "Battle1st");
    MR::addEffectHitNormal(mParent, nullptr);
    initParts();
}

namespace {
    TombSpiderGland* createGland(TombSpider* pParent, const char* pName, const Vec3f& rPos, const Vec3f& rRotate, const char* pJointName) {
        TombSpiderGland* gland = new TombSpiderGland(pParent, pName, rPos, rRotate, pJointName);
        gland->initWithoutIter();
        MR::registerDemoSimpleCastAll(gland);
        return gland;
    }

    TombSpiderVitalSpot* createVitalSpot(TombSpider* pParent, const char* pName, const Vec3f& rPos, const Vec3f& rRotate, const char* pJointName) {
        TombSpiderVitalSpot* vitalSpot = new TombSpiderVitalSpot(pParent, pName, rPos, rRotate, pJointName);
        vitalSpot->initWithoutIter();
        MR::registerDemoSimpleCastAll(vitalSpot);
        return vitalSpot;
    }

    TombSpiderThreadAttacher* createThreadAttacher(TombSpider* pParent, const char* pJointName, f32 radius, f32 offset) {
        TombSpiderThreadAttacher* pAttacher = new TombSpiderThreadAttacher(pParent, pJointName, radius, offset);
        pAttacher->initWithoutIter();
        return pAttacher;
    }

    TombSpiderThreadAttacher* createThreadAttacherFoot(TombSpider* pParent, const char* pJointName) {
        return createThreadAttacher(pParent, pJointName, ::sAttachFootRadius, ::sAttachFootOffset);
    }
};  // namespace

void TombSpiderParts::initParts() {
    mGlandFrontL = ::createGland(mParent, "毒腺[左前]", Vec3f(cNum_initParts_f1, 0.0f, -cNum_initParts_f2), Vec3f(-cNum_initParts_f3, -cNum_initParts_f4, 0.0f), "FrontL");
    mGlandFrontR = ::createGland(mParent, "毒腺[右前]", Vec3f(-cNum_initParts_f5, 0.0f, -cNum_initParts_f6), Vec3f(-cNum_initParts_f7, cNum_initParts_f8, 0.0f), "FrontR");
    mGlandRearL = ::createGland(mParent, "毒腺[左後]", Vec3f(cNum_initParts_f9, 0.0f, -cNum_initParts_f10), Vec3f(-cNum_initParts_f11, -cNum_initParts_f12, 0.0f), "RearL");
    mGlandRearR = ::createGland(mParent, "毒腺[右後]", Vec3f(-cNum_initParts_f13, 0.0f, -cNum_initParts_f14), Vec3f(-cNum_initParts_f15, cNum_initParts_f16, 0.0f), "RearR");

    mVitalSpotC = ::createVitalSpot(mParent, "急所[中]", Vec3f(0.0f, 0.0f, -cNum_initParts_f17), Vec3f(0.0f, cNum_initParts_f18, 0.0f), "Body5");
    mVitalSpotL = ::createVitalSpot(mParent, "急所[左]", Vec3f(cNum_initParts_f19, -cNum_initParts_f20, -cNum_initParts_f21), Vec3f(cNum_initParts_f22, cNum_initParts_f23, 0.0f), "Body1");
    mVitalSpotR = ::createVitalSpot(mParent, "急所[右]", Vec3f(cNum_initParts_f24, cNum_initParts_f25, -cNum_initParts_f26), Vec3f(-cNum_initParts_f27, cNum_initParts_f28, 0.0f), "Body1");

    mThreadAttacherWing = ::createThreadAttacher(mParent, "WingL", ::sAttachBodyBackRadius, ::sAttachBodyBackOffset);

    mThreadAttacherFrontL = ::createThreadAttacherFoot(mParent, "FrontHandL3");
    mThreadAttacherCenterL = ::createThreadAttacherFoot(mParent, "CenterHandL3");
    mThreadAttacherRearL = ::createThreadAttacherFoot(mParent, "RearHandL3");
    mThreadAttacherFrontR = ::createThreadAttacherFoot(mParent, "FrontHandR3");
    mThreadAttacherCenterR = ::createThreadAttacherFoot(mParent, "CenterHandR3");
    mThreadAttacherRearR = ::createThreadAttacherFoot(mParent, "RearHandR3");

    mMtxEye = MR::getJointMtx(mParent, "Eye");
    mMtxMouth = MR::getJointMtx(mParent, "Mouth");
    mMtxHip = MR::getJointMtx(mParent, "Hip");
    mMtxGlandFrontL = MR::getJointMtx(mParent, "FrontL");
    mMtxGlandFrontR = MR::getJointMtx(mParent, "FrontR");
    mMtxGlandRearL = MR::getJointMtx(mParent, "RearL");
    mMtxGlandRearR = MR::getJointMtx(mParent, "RearR");

    mAcid = new TombSpiderAcid(mParent);
}
