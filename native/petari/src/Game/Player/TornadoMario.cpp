#include "Game/Player/TornadoMario.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 3000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 50.0f);

TornadoMario::TornadoMario(const char* pName) : LiveActor(pName), mUp(0.0f, 1.0f, 0.0f) {
}

void TornadoMario::init(const TableIter& rIter) {
    initModelManagerWithAnm(mName, nullptr, true);
    initSound(4, false);
    MR::connectToScene(this, MR::MovementType_Player, MR::CalcAnimType_Player, MR::DrawBufferType_PlayerDecoration, MR::DrawType_None);
    MR::initMaterialFog(this, true);
    MR::prepareMaterials(this);
    MR::initJointTransform(this);
    MR::initShadowVolumeCylinder(this, cNum_init_f1);
    MR::setShadowDropLength(this, nullptr, cNum_init_f2);
    MR::onShadowVolumeCutDropLength(this, nullptr);
    MR::setShadowVolumeEndDropOffset(this, nullptr, cNum_init_f3);
    MR::onCalcShadow(this, nullptr);
    MR::invalidateClipping(this);
    appear();
    mIsVisible = false;
    MR::hideModel(this);
}

void TornadoMario::hideForce() {
    if (mIsVisible) {
        mIsVisible = false;
        MR::startBck(this, "MarioTornadoStart");
        MR::hideModel(this);
    }
}

void TornadoMario::calcAnim() {
    LiveActor::calcAnim();
}

void TornadoMario::setTrHeight(const Vec3f& rPos, const Vec3f& rFront, const Vec3f& rUp, const Vec3f& rShadowDir) {
    mPosition = rPos;
    mUp = rUp;
    mFront = rFront;
    MR::setShadowDropDirection(this, nullptr, rShadowDir);
}

void TornadoMario::calcAndSetBaseMtx() {
    Pos3f pos;
    MR::makeMtxUpFrontPos(&pos, mUp, mFront, mPosition);
    MR::setBaseTRMtx(this, pos);
}

TornadoMario::~TornadoMario() {
}

void TornadoMario::show() {
    if (!mIsVisible) {
        mIsVisible = true;
        MR::startBck(this, "MarioTornadoStart");
        MR::startBtk(this, "MarioTornadoStart");
        MR::showModel(this);
    }
}

void TornadoMario::hide() {
    if (mIsVisible) {
        mIsVisible = false;
        MR::startBck(this, "MarioTornadoEnd");
        MR::startBtk(this, "MarioTornadoEnd");
    }
}

void TornadoMario::control() {
    if (mIsVisible) {
        if (MR::isBckOneTimeAndStopped(this)) {
            MR::startBck(this, "MarioTornadoLoop");
            MR::startBck(this, "MarioTornadoLoop");
        }

        MR::startLevelSound(this, "SE_PM_LV_TORNADE_FLYING");
    } else {
        if (MR::isBckOneTimeAndStopped(this)) {
            MR::hideModel(this);
        }
    }
}
