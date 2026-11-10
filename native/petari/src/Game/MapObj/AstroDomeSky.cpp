#include "Game/MapObj/AstroDomeSky.hpp"
#include "Game/LiveActor/MaterialCtrl.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/SphereSelector.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_exeRotateDisappear_f1, "exeRotateDisappear.f1", 20.0f);

namespace {
    NEBULA_TUNABLE_RECORD(Vec, cJumpOutDemoRotate, {347.0f, 0.0f, 0.0f});
    const char* const cAstroDomeSkyTable[] = {"AstroDomeSkyA", "AstroDomeSkyB", "AstroDomeSkyC", "AstroDomeSkyA", "AstroDomeSkyB", "AstroDomeSkyC"};
    NEBULA_TUNABLE(s32, cAppearanceBrkTotalFrame, 200);
    NEBULA_TUNABLE(s32, cRotateDisappearBrkFrame, 30);
    NEBULA_TUNABLE(s32, cRotateAppearBrkFrame, 59);
    NEBULA_TUNABLE(f32, cAppearanceRotateSpeed, 0.1f);

    const NebulaColor8 sColor1 = {0x00, 0x00, 0x00, 0xFF};
    const NebulaColor8 sColor2 = {0x00, 0x00, 0x00, 0xFF};
};  // namespace

namespace NrvAstroDomeSky {
    NEW_NERVE(AstroDomeSkyNrvHide, AstroDomeSky, Hide);
    NEW_NERVE(AstroDomeSkyNrvAppear, AstroDomeSky, Appear);
    NEW_NERVE(AstroDomeSkyNrvWait, AstroDomeSky, Wait);
    NEW_NERVE(AstroDomeSkyNrvRotateAppear, AstroDomeSky, RotateAppear);
    NEW_NERVE(AstroDomeSkyNrvRotateDisappear, AstroDomeSky, RotateDisappear);
    NEW_NERVE(AstroDomeSkyNrvDisappear, AstroDomeSky, Disappear);
    NEW_NERVE(AstroDomeSkyNrvReturnDemoWait, AstroDomeSky, ReturnDemoWait);
    NEW_NERVE(AstroDomeSkyNrvJumpOutDemo, AstroDomeSky, JumpOutDemo);
};  // namespace NrvAstroDomeSky

AstroDomeSky::AstroDomeSky(const char* pName) : LiveActor(pName), mBrkFrame(), mColor(0x00, 0x00, 0x00, 0xFF), mProjmap() {
}

void AstroDomeSky::init(const TableIter& rIter) {
    s32 domeId = -1;
    MR::getPlacementArg0NoInit(rIter, &domeId);

    // A dome past the six (a mod's) takes the skies round again; no dome number takes the first.
    const s32 skyCount = static_cast< s32 >(ARRAY_SIZE(::cAstroDomeSkyTable));
    const char* pSkyName = ::cAstroDomeSkyTable[domeId >= 1 ? (domeId - 1) % skyCount : 0];
    initModelManagerWithAnm(pSkyName, nullptr, true);

    mProjmap = MR::initMaterialProjmapEffectMtxSetter(this);
    MR::prepareMaterials(this);

    MR::startBtk(this, pSkyName);

    MR::connectToScene(this, MR::MovementType_Sky, MR::CalcAnimType_MapObj, MR::DrawBufferType_AstroDomeSky, MR::DrawType_AstroDomeSkyClear);
    MR::invalidateClipping(this);

    initNerve(GET_NERVE(AstroDomeSky, AstroDomeSkyNrvHide));

    MR::tryRegisterDemoCast(this, rIter);
    MR::registerDemoActionNerve(this, GET_NERVE(AstroDomeSky, AstroDomeSkyNrvReturnDemoWait), "移動");
    MR::registerDemoActionNerve(this, GET_NERVE(AstroDomeSky, AstroDomeSkyNrvDisappear), "ドーム出現");
    MR::registerDemoCast(this, "マリオ飛び出し", rIter);
    MR::registerDemoSimpleCastAll(this);

    SphereSelectorFunction::registerTarget(this);

    makeActorAppeared();
}

void AstroDomeSky::draw() const {
    MR::fillScreen(mColor.mRGBA);
}

void AstroDomeSky::calcAndSetBaseMtx() {
    Pos3f rotateMtx;
    if (isNerve(GET_NERVE(AstroDomeSky, AstroDomeSkyNrvJumpOutDemo))) {
        MR::makeMtxRotate(rotateMtx, ::cJumpOutDemoRotate);
    } else {
        SphereSelectorFunction::calcHandledRotateMtx(Vec3f(0.0f, 0.0f, 0.0f), &rotateMtx);
    }

    rotateMtx.setTrans(MR::getCamPos());
    MR::setBaseTRMtx(this, rotateMtx);

    mProjmap->updateMtxUseBaseMtx();
}

bool AstroDomeSky::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (SphereSelectorFunction::isMsgSelectStart(msg)) {
        setNerve(GET_NERVE(AstroDomeSky, AstroDomeSkyNrvAppear));
        return true;
    }

    if (SphereSelectorFunction::isMsgSelectEnd(msg)) {
        setNerve(GET_NERVE(AstroDomeSky, AstroDomeSkyNrvDisappear));
        return true;
    }

    return false;
}

bool AstroDomeSky::tryRotateAppearance() {
    if (::cAppearanceRotateSpeed < MR::abs(SphereSelectorFunction::getHandleRotateSpeed())) {
        if (!isNerve(GET_NERVE(AstroDomeSky, AstroDomeSkyNrvRotateDisappear))) {
            if (isNerve(GET_NERVE(AstroDomeSky, AstroDomeSkyNrvRotateAppear)) && !MR::isBrkStopped(this)) {
                mBrkFrame = ::cRotateAppearBrkFrame - MR::getBrkFrame(this);
            } else {
                mBrkFrame = 0.0f;
            }

            setNerve(GET_NERVE(AstroDomeSky, AstroDomeSkyNrvRotateDisappear));

            return true;
        }
    } else {
        if (isNerve(GET_NERVE(AstroDomeSky, AstroDomeSkyNrvRotateDisappear))) {
            if (!MR::isBrkStopped(this)) {
                mBrkFrame = ::cRotateAppearBrkFrame - MR::getBrkFrame(this);
            } else {
                mBrkFrame = ::cRotateDisappearBrkFrame - 1.0f;
            }

            setNerve(GET_NERVE(AstroDomeSky, AstroDomeSkyNrvRotateAppear));

            return true;
        }
    }

    return false;
}

void AstroDomeSky::exeHide() {
    if (MR::isFirstStep(this)) {
        MR::startBrk(this, "Disappear");
        MR::setBrkFrameAndStop(this, MR::getBrkFrameMax(this, "Disappear"));

        mColor.set(::sColor1);
    }
}

void AstroDomeSky::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::startBrk(this, "Appear");

        MR::startSystemSE("SE_DM_ASTRO_HANDLE_OPEN_ST");
    }

    f32 t = MR::calcNerveRate(this, MR::getBrkFrameMax(this, "Appear"));
    mColor.set(MR::lerp(::sColor1, ::sColor2, t));

    if (!MR::isBrkStopped(this)) {
        return;
    }

    MR::startSystemSE("SE_DM_ASTRO_HANDLE_OPEN_ED");

    setNerve(GET_NERVE(AstroDomeSky, AstroDomeSkyNrvWait));
}

void AstroDomeSky::exeWait() {
    if (MR::isFirstStep(this)) {
        mColor.set(::sColor2);
    }

    if (tryRotateAppearance()) {
        return;
    }
}

void AstroDomeSky::exeRotateAppear() {
    if (MR::isFirstStep(this)) {
        MR::startBrk(this, "Appear");
        MR::setBrkFrame(this, mBrkFrame);
        MR::setBrkRate(this, (::cRotateAppearBrkFrame - mBrkFrame) / ::cAppearanceBrkTotalFrame);
    }

    if (tryRotateAppearance()) {
        return;
    }
}

void AstroDomeSky::exeRotateDisappear() {
    if (MR::isFirstStep(this)) {
        MR::startBrk(this, "Disappear");
        MR::setBrkFrame(this, mBrkFrame);
        MR::setBrkRate(this, (::cRotateDisappearBrkFrame - mBrkFrame) / cNum_exeRotateDisappear_f1);
    }

    if (::cRotateDisappearBrkFrame <= MR::getBrkFrame(this)) {
        MR::setBrkFrameAndStop(this, ::cRotateDisappearBrkFrame);
    }

    if (tryRotateAppearance()) {
        return;
    }
}

void AstroDomeSky::exeDisappear() {
    if (MR::isFirstStep(this)) {
        MR::startBrk(this, "Disappear");
    }

    f32 t = MR::calcNerveRate(this, MR::getBrkFrameMax(this, "Disappear"));
    mColor.set(MR::lerp(::sColor1, ::sColor2, t));

    if (!MR::isBrkStopped(this)) {
        return;
    }

    setNerve(GET_NERVE(AstroDomeSky, AstroDomeSkyNrvHide));
}

void AstroDomeSky::exeReturnDemoWait() {
    if (MR::isFirstStep(this)) {
        MR::startBrk(this, "Appear");
        MR::setBrkFrameAndStop(this, MR::getBrkFrameMax(this, "Appear"));

        mColor.set(::sColor1);
    }
}
