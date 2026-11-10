#include "Game/Map/FileSelectSky.hpp"
#include "Game/LiveActor/MaterialCtrl.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f1, "exeWait.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f2, "exeWait.f2", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f3, "exeWait.f3", 4.0f);

namespace {
    NEBULA_TUNABLE(f32, cAngleIncY, 0.001f);
    NEBULA_TUNABLE(f32, cCycleX, 3000.0f);
};  // namespace

namespace NrvFileSelectSky {
    NEW_NERVE(FileSelectSkyNrvWait, FileSelectSky, Wait);
};  // namespace NrvFileSelectSky

FileSelectSky::~FileSelectSky() {
}

FileSelectSky::FileSelectSky(const char* pName) : LiveActor(pName), mAngleX(), mAngleY(), mProjmapEffectMtxSetter() {
}

void FileSelectSky::init(const TableIter& /*rIter*/) {
    initModelManagerWithAnm("CometNearOrbitSky", nullptr, false);
    mScale.x = cNum_init_f1;
    mScale.y = cNum_init_f2;
    mScale.z = cNum_init_f3;
    mProjmapEffectMtxSetter = MR::initMaterialProjmapEffectMtxSetter(this);
    MR::connectToSceneSky(this);
    initEffectKeeper(0, nullptr);
    MR::invalidateClipping(this);
    initNerve(GET_NERVE(FileSelectSky, FileSelectSkyNrvWait));
    makeActorDead();
}

void FileSelectSky::calcAnim() {
    LiveActor::calcAnim();
    mProjmapEffectMtxSetter->updateMtxUseBaseMtx();
}

void FileSelectSky::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mBaseMtx);
}

bool FileSelectSky::receiveOtherMsg(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return false;
}

void FileSelectSky::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "CometNearOrbitSky");
        MR::startBtk(this, "CometNearOrbitSky");
    }

    if (MR::isFirstStep(this)) {
        mAngleX = 0.0f;
        mAngleY = 0.0f;
    }

    Pos3f rotateX, rotateY;
    rotateY.makeRotate(Vec3f(0.0f, 1.0f, 0.0f), mAngleY);
    rotateX.makeRotate(Vec3f(1.0f, 0.0f, 0.0f), mAngleX);
    mBaseMtx.concat(rotateY, rotateX);
    mBaseMtx.invert(mBaseMtx);
    f32 step = (getNerveStep() * PI) / ::cCycleX;

    mAngleX = cNum_exeWait_f1 * (1.0f - MR::cos(step)) * MR::pi() * cNum_exeWait_f2 / cNum_exeWait_f3;
    mAngleY += ::cAngleIncY;
}
