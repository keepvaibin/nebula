#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Scene/MultiSceneActor.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/LiveActor/Spine.hpp"
#include "Game/Scene/MultiSceneEffectKeeper.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Render/Clip.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcAnim_f1, "calcAnim.f1", 1000.0f);

MultiSceneActor::MultiSceneActor(const char* pName, const char* pModelName, bool deferMaterials)
    : NameObj(pName), mTranslation(0.0f, 0.0f, 0.0f), mRotation(0.0f, 0.0f, 0.0f), mScale(1.0f, 1.0f, 1.0f), mIsDead(true), mModelManager(nullptr),
      mEffectSystem(nullptr), mEffectKeeper(nullptr), mSpine(nullptr), mIsScenarioSelectSky(MR::isEqualString(pName, "シナリオ選択の空")) {
    mModelManager = new ModelManager();
    mModelManager->init(pModelName, nullptr, deferMaterials);
}

void MultiSceneActor::init(const TableIter& /*rIter*/) {
}

void MultiSceneActor::movement() {
    if (mIsDead) {
        return;
    }

    mModelManager->update();

    if (mSpine != nullptr) {
        mSpine->update();
    }

    if (mIsDead) {
        return;
    }

    control();
}

void MultiSceneActor::calcAnim() {
    if (mIsDead) {
        return;
    }

    Model* pModel = mModelManager->getModel();
    MR::makeMtxTR(pModel->mBaseTransformMtx, mTranslation, mRotation);
    pModel->setBaseScale(mScale);
    // The star-select sky (ScenarioSelectLayout) is one quad at z = -1000 sized for 16:9: a wider picture scales it
    // about its centre so it still covers the screen. The base matrix is rotation and translation only.
    if (MR::isScreen16Per9() && MR::getDisplay().wide &&
        mIsScenarioSelectSky) {
        const f32 k = MR::getDisplay().layout_width / NebulaFrameClock::cLayoutWidth;
        if (k > 1.0f) {
            const f32 offsetZ = pModel->mBaseScale.z * (1.0f - k) * -cNum_calcAnim_f1;
            pModel->mBaseScale.x *= k;
            pModel->mBaseScale.y *= k;
            pModel->mBaseScale.z *= k;
            for (s32 row = 0; row < 3; row++) {
                pModel->mBaseTransformMtx[row][3] += pModel->mBaseTransformMtx[row][2] * offsetZ;
            }
        }
    }

    mModelManager->calcAnim();
}

void MultiSceneActor::calcViewAndEntry() {
    if (mIsDead) {
        return;
    }

    mModelManager->calcView();
    mModelManager->entry();
}

void MultiSceneActor::appear() {
    mIsDead = false;

    calcAnim();
}

void MultiSceneActor::kill() {
    if (mEffectKeeper != nullptr) {
        mEffectKeeper->clear();
    }

    mIsDead = true;
}

ModelManager* MultiSceneActor::getModelManager() const {
    return mModelManager;
}

EffectSystem* MultiSceneActor::getEffectSystem() const {
    return mEffectSystem;
}

MultiSceneEffectKeeper* MultiSceneActor::getEffectKeeper() const {
    return mEffectKeeper;
}

void MultiSceneActor::initEffect(EffectSystem* pEffectSystem, int extraEmitterNum, const char* pResName) {
    mEffectSystem = pEffectSystem;

    mEffectKeeper = new MultiSceneEffectKeeper(mModelManager, extraEmitterNum, pResName);
    mEffectKeeper->init(this, pEffectSystem);
}

void MultiSceneActor::setNerve(const Nerve* pNerve) {
    mSpine->setNerve(pNerve);
}

bool MultiSceneActor::isNerve(const Nerve* pNerve) const {
    return mSpine->getCurrentNerve() == pNerve;
}

void MultiSceneActor::initNerve(const Nerve* pNerve) {
    mSpine = new Spine(this, pNerve);
}

s32 MultiSceneActor::getNerveStep() const {
    return mSpine->mStep;
}

namespace MultiScene {
    void startBck(MultiSceneActor* pActor, const char* pBckName) {
        pActor->getModelManager()->startBck(pBckName, nullptr);
    }

    void startBtk(MultiSceneActor* pActor, const char* pBtkName) {
        pActor->getModelManager()->startBtk(pBtkName);
    }

    void startBtp(MultiSceneActor* pActor, const char* pBtpName) {
        pActor->getModelManager()->startBtp(pBtpName);
    }

    void startBva(MultiSceneActor* pActor, const char* pBvaName) {
        pActor->getModelManager()->startBva(pBvaName);
    }

    void setBtpFrameAndStop(MultiSceneActor* pActor, f32 frame) {
        pActor->getModelManager()->getBtpCtrl()->setFrame(frame);
        pActor->getModelManager()->getBtpCtrl()->setRate(0.0f);
    }

    void setBvaFrameAndStop(MultiSceneActor* pActor, f32 frame) {
        pActor->getModelManager()->getBvaCtrl()->setFrame(frame);
        pActor->getModelManager()->getBvaCtrl()->setRate(0.0f);
    }

    bool isBckStopped(MultiSceneActor* pActor) {
        return pActor->getModelManager()->isBckStopped();
    }

    bool isStep(const MultiSceneActor* pActor, s32 step) {
        return pActor->getNerveStep() == step;
    }

    bool isFirstStep(const MultiSceneActor* pActor) {
        return pActor->getNerveStep() == 0;
    }

    bool isGreaterStep(const MultiSceneActor* pActor, s32 step) {
        return pActor->getNerveStep() > step;
    }

    bool isLessStep(const MultiSceneActor* pActor, s32 step) {
        return pActor->getNerveStep() < step;
    }

    bool isLessEqualStep(const MultiSceneActor* pActor, s32 step) {
        return pActor->getNerveStep() <= step;
    }

    void setNerveAtStep(MultiSceneActor* pActor, const Nerve* pNerve, s32 step) {
        if (isStep(pActor, step)) {
            pActor->setNerve(pNerve);
        }
    }

    void setNerveAtBckStopped(MultiSceneActor* pActor, const Nerve* pNerve) {
        if (isBckStopped(pActor)) {
            pActor->setNerve(pNerve);
        }
    }

    f32 calcNerveRate(const MultiSceneActor* pActor, s32 stepMax) {
        return stepMax <= 0 ? 1.0f : MR::clamp(static_cast< f32 >(pActor->getNerveStep()) / stepMax, 0.0f, 1.0f);
    }

    f32 calcNerveRate(const MultiSceneActor* pActor, s32 stepMin, s32 stepMax) {
        return MR::clamp(MR::normalize(pActor->getNerveStep(), stepMin, stepMax), 0.0f, 1.0f);
    }

    f32 calcNerveEaseOutRate(const MultiSceneActor* pActor, s32 stepMax) {
        return MR::getEaseOutValue(calcNerveRate(pActor, stepMax), 0.0f, 1.0f, 1.0f);
    }

    f32 calcNerveValue(const MultiSceneActor* pActor, s32 stepMax, f32 valueStart, f32 valueEnd) {
        return MR::getLinerValue(calcNerveRate(pActor, stepMax), valueStart, valueEnd, 1.0f);
    }

    void emitEffect(const MultiSceneActor* pActor, const char* pEffectName) {
        pActor->getEffectKeeper()->create(pEffectName, pActor->getEffectSystem());
    }

    void deleteEffectAll(const MultiSceneActor* pActor) {
        pActor->getEffectKeeper()->deleteAll();
    }

    void forceDeleteEffectAll(const MultiSceneActor* pActor) {
        pActor->getEffectKeeper()->forceDeleteAll(pActor->getEffectSystem());
    }

    MtxPtr getJointMtx(const MultiSceneActor* pActor, const char* pJointName) {
        return MR::getJointMtx(pActor->getModelManager()->getModel(), pJointName);
    }
};  // namespace MultiScene

void MultiSceneActor::control() {
}
