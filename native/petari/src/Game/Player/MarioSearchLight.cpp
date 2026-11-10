#include "Game/Player/MarioSearchLight.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Player/DrawAdaptor.hpp"
#include "Game/Player/PlayerModel.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioSwim.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/FixedPosition.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/NebulaArena.hpp"
#include <nebula_math.h>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f1, "updateHitSensor.f1", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f2, "updateHitSensor.f2", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f1, "calcAndSetBaseMtx.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_initThrowing_f1, "initThrowing.f1", 13.19f);
NEBULA_TUNABLE_NAMED(f32, cNum_initThrowing_f2, "initThrowing.f2", 41.57f);
NEBULA_TUNABLE_NAMED(f32, cNum_initThrowing_f3, "initThrowing.f3", 19.02f);
NEBULA_TUNABLE_NAMED(f32, cNum_initThrowing_f4, "initThrowing.f4", 3.63f);
NEBULA_TUNABLE_NAMED(f32, cNum_initThrowing_f5, "initThrowing.f5", 41.69f);
NEBULA_TUNABLE_NAMED(f32, cNum_initThrowing_f6, "initThrowing.f6", 15.76f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawSearchLight_f1, "drawSearchLight.f1", 10000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawSearchLight_f2, "drawSearchLight.f2", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawSearchLight_f3, "drawSearchLight.f3", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawSearchLight_f4, "drawSearchLight.f4", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawSearchLight_f5, "drawSearchLight.f5", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawSearchLight_f6, "drawSearchLight.f6", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawSearchLight_f7, "drawSearchLight.f7", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawSearchLight_f8, "drawSearchLight.f8", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawSearchLight_f9, "drawSearchLight.f9", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawSearchLight_f10, "drawSearchLight.f10", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawSearchLight_f11, "drawSearchLight.f11", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawSearchLight_f12, "drawSearchLight.f12", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawSearchLight_f13, "drawSearchLight.f13", 4.0f);

MarioSearchLight::MarioSearchLight(LiveActor* pActor) : PartsModel(pActor, "サーチライト", "SearchLightCone", nullptr, -1, 0), mBeamDistance(), mHitTimer(), mTarget() {
}

void MarioSearchLight::init(const TableIter& rParam1) {
    PartsModel::init(rParam1);
    initHitSensor(1);
    mBeamDistance = 0.0f;
    mTarget = nullptr;
    mHitTimer = 0;
    MR::addHitSensorCallback(this, "eye", ATYPE_EYE, 4, cNum_init_f1);
}

void MarioSearchLight::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        return;
    }

    if (MR::sendArbitraryMsg(ACTMES_SEARCHLIGHT_ATTACK, pReceiver, pSender)) {
        return;
    }

    if (!MR::isExistInAttributeGroupSearchTurtle(pReceiver->mHost)) {
        return;
    }

    if (pReceiver != mTarget) {
        MR::tryRumblePadWeak(this, PLAYER_1);
    }

    mTarget = pReceiver;
    mHitTimer = 30;
}

void MarioSearchLight::updateHitSensor(HitSensor* pSensor) {
    Vec3f frontVec;
    MR::calcFrontVec(&frontVec, this);

    Vec3f frontPosition = mPosition + frontVec * mBeamDistance;

    pSensor->mPosition.x = frontPosition.x;
    pSensor->mPosition.y = frontPosition.y;
    pSensor->mPosition.z = frontPosition.z;

    mBeamDistance += cNum_updateHitSensor_f1;
    if (mBeamDistance > cNum_updateHitSensor_f2) {
        mBeamDistance = 0.0f;
    }

    if (mTarget != nullptr) {
        if (!mTarget->mValidByHost) {
            mTarget = nullptr;
        }
    }

    if (mHitTimer != 0) {
        mHitTimer--;
    }

    if (mHitTimer == 0) {
        mTarget = nullptr;
    }
}

void MarioSearchLight::calcAndSetBaseMtx() {
    Mtx baseMtx;
    NebulaMtxCopy(getBaseMtx(), baseMtx);

    PartsModel::calcAndSetBaseMtx();
    MR::blendMtxRotateSlerp(baseMtx, getBaseMtx(), cNum_calcAndSetBaseMtx_f1, baseMtx);

    Vec3f trans;
    MR::extractMtxTrans(getBaseMtx(), &trans);
    MR::setMtxTrans(baseMtx, trans.x, trans.y, trans.z);
    MR::setBaseTRMtx(this, baseMtx);
}

void MarioActor::initThrowing() {
    mSearchLightThrowPos = new FixedPosition(this, "HandR", Vec3f(cNum_initThrowing_f1, cNum_initThrowing_f2, cNum_initThrowing_f3), Vec3f(-cNum_initThrowing_f4, -cNum_initThrowing_f5, cNum_initThrowing_f6));
}

void MarioActor::updateSearchLight() {
    if (mSearchLight == nullptr) {
        return;
    }

    if (mMario->isStatusActive(MarioStatus_Swim) && (mMario->mSwim->mJetTimer != 0)) {
        if (MR::isDead(mSearchLight)) {
            mSearchLight->appear();
            MR::hideModel(mSearchLight);
            MR::emitEffect(mSearchLight, "Glow");
        }
    } else {
        mSearchLight->kill();
    }
}

namespace {
    inline PlayerModel* getModelX(LiveActor* pActor) {
        return static_cast< PlayerModel* >(MR::getModel(pActor));
    }
};  // namespace

void MarioActor::initSearchLight() {
    mSearchLight = new MarioSearchLight(this);
    mSearchLight->initWithoutIter();
    mSearchLight->makeActorDead();
    PlayerModel* model = static_cast< PlayerModel* >(MR::getModel(mSearchLight));
    model->initExtraPoses(16);

    new DrawAdaptor(MR::Functor(this, &MarioActor::drawSearchLight), MR::DrawType_PlayerSearchLight);
}

void MarioActor::updateThrowing() {
    if (mSearchLightThrowPos == nullptr) {
        return;
    }

    mSearchLightThrowPos->calc();

    if (mSearchLight == nullptr) {
        return;
    }

    Vec3f trans;
    mSearchLightThrowPos->mMtx.getTrans(trans);

    Vec3f& position = mSearchLight->mPosition;
    position = trans + mVelocity;

    mSearchLightThrowPos->copyRotate(&mSearchLight->mRotation);

    MR::calcAnimDirect(mSearchLight);
}

void MarioActor::calcViewSearchLight() {
    if (mSearchLight == nullptr) {
        return;
    }

    if (!MR::isDead(mSearchLight)) {
        PlayerModel* model = ::getModelX(mSearchLight);

        // The search light is has three scaled cone "shells" to fake a blur effect.
        Mtx modelMdx;
        MR::multMtx(modelMdx, model->getBaseTRMtx(), ModelSys::getViewMtx());

        Mtx invBaseMtx;
        NebulaMtxInverse(model->getBaseTRMtx(), invBaseMtx);

        model->viewCalc3(0, nullptr);

        for (u32 i = 0; i < ARRAY_SIZE(mConst->getTable()->mSearchLightBlurScale); i++) {
            Mtx scaledMtx;
            f32 scale = mConst->getTable()->mSearchLightBlurScale[i];
            NebulaMtxScale(scaledMtx, scale, scale, scale);

            MR::multMtx(scaledMtx, scaledMtx, modelMdx);
            MR::multMtx(ModelSys::getViewMtx(), invBaseMtx, scaledMtx);

            model->viewCalc3(i + 1, nullptr);
        }

        MR::loadViewMtx();
        mSearchLightTimer++;
    } else {
        mSearchLightTimer = 0;
    }
}

void MarioActor::drawSearchLight() const {
    if (mSearchLight == nullptr) {
        return;
    }

    if (!(mMario->isStatusActive(MarioStatus_Swim) && mMario->mSwim->mJetTimer != 0)) {
        return;
    }

    if (MR::isSystemTalking()) {
        return;
    }

    if (mMario->mSwim->mJetCooldownTimer > 0) {
        return;
    }

    Vec3f throwVec;
    getThrowVec(&throwVec);

    Vec2f screenPos1;
    TDDraw::project2D(&screenPos1, mPosition);

    Vec2f screenPos2;
    TDDraw::project2D(&screenPos2, mPosition + throwVec * cNum_drawSearchLight_f1);

    Vec2f minimum;
    Vec2f maximum;
    if (screenPos1.x >= screenPos2.x) {
        minimum.x = screenPos2.x;
        maximum.x = screenPos1.x;
    } else {
        minimum.x = screenPos1.x;
        maximum.x = screenPos2.x;
    }

    if (screenPos1.y >= screenPos2.y) {
        minimum.y = screenPos2.y;
        maximum.y = screenPos1.y;
    } else {
        minimum.y = screenPos1.y;
        maximum.y = screenPos2.y;
    }

    screenPos1 = minimum + Vec2f(-cNum_drawSearchLight_f2, -cNum_drawSearchLight_f3);
    screenPos2 = maximum + Vec2f(cNum_drawSearchLight_f4, cNum_drawSearchLight_f5);

    // The light model with its own material and the passes over it; setup's view only.
    SiteMaterial view;
    TDDraw::setupNative(&view, 0, 1, 1);

    PlayerModel* model = static_cast< PlayerModel* >(MR::getModel(mSearchLight));
    Material* const material = model->getModelData()->getMaterialAt(0);
    MaterialDraw draw;
    if (model->simpleDrawSetup(material, &draw)) {
        // Gradually fade in the search light.
        f32 lightBrightness = 1.0f;
        if (mSearchLightTimer < 30) {
            lightBrightness = 0.0f;
        } else if (mSearchLightTimer < 180) {
            lightBrightness = (mSearchLightTimer - 30) / cNum_drawSearchLight_f6;
        }

        // The channel colour at alpha A0.
        const u8 volumeAlpha = (u8)(s32)((lightBrightness * mConst->getTable()->mSearchLightColor[3]));
        draw.override(2, volumeAlpha / cNum_drawSearchLight_f7, 0.0f, -1.0f);

        // Render intersection of light cone with the scene by subtracting
        // visible back faces from front faces.
        PassState state = PassStates::fromWord(draw.getState());
        PassStates::setDepth(state, true, NEBULA_COMPARE_LEQUAL, false);
        state.alpha_write = false;
        state.color_write = false;
        PassStates::setNoBlend(state);
        state.stencil_func = NEBULA_STENCIL_ALWAYS;
        state.stencil_op = NEBULA_STENCIL_ADD;
        state.cull = NEBULA_CULL_BACK;
        draw.setState(state.word());
        model->setDrawView(0);
        model->simpleDrawShape(material, draw);

        state.stencil_op = NEBULA_STENCIL_TAKE;
        state.cull = NEBULA_CULL_FRONT;
        draw.setState(state.word());
        model->simpleDrawShape(material, draw);

        // The light's colour added over the box where the cone was marked, at its strength; the mark goes with it.
        TDDraw::setupNative(&view, 0, 1, 2);
        view.setBlend(NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_ONE);
        view.setStencil(NEBULA_STENCIL_IF_SET, NEBULA_STENCIL_ZERO);

        u8* col = mConst->getTable()->mSearchLightColor;
        {
            TypedDraw site = TDDraw::site(view, 2);
            TDDraw::drawFillBox(site, screenPos1, screenPos2, col[0] << 24 | col[1] << 16 | col[2] << 8 | volumeAlpha);
        }

        TDDraw::setupNative(&view, 0, 1, 1);
        bool res = model->simpleDrawSetup(material, &draw);

        state = PassStates::fromWord(draw.getState());
        PassStates::setDepth(state, true, NEBULA_COMPARE_LEQUAL, false);
        PassStates::setBlend(state, NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_ONE);
        state.alpha_write = false;
        draw.setState(state.word());

        // Colour of the light cone: the channel colour at alpha RASA - K(1 - A0), drawn above 4.
        const u8 coneAlpha = (u8)(s32)((lightBrightness * mConst->getTable()->mSearchLightAlphaLevel));
        draw.override(3, coneAlpha / cNum_drawSearchLight_f8, 0.0f, cNum_drawSearchLight_f9);

        if (res) {
            // Draw central light cone.
            model->simpleDrawShape(material, draw);
        } else {
            return;
        }

        // Draw soft falloff outside light cone.
        for (u32 i = 0; i < ARRAY_SIZE(mConst->getTable()->mSearchLightBlurAlpha); i++) {
            model->setDrawView(i + 1);

            // Cone falloff colour (A0) and end cap falloff colour (A1): the channel colour at (A1 + (A0 - A1) RASA) / 2.
            const u8 blurAlpha = (u8)(s32)((lightBrightness * mConst->getTable()->mSearchLightBlurAlpha[i]));
            draw.override(4, blurAlpha / cNum_drawSearchLight_f10, cNum_drawSearchLight_f11 / cNum_drawSearchLight_f12, cNum_drawSearchLight_f13);

            model->simpleDrawShape(material, draw);
        }
    }
}

void MarioActor::getThrowVec(Vec3f* pVec) const {
    Vec3f rotate;
    mSearchLightThrowPos->copyRotate(&rotate);

    Vec3f dirZ(0.0f, 0.0f, 1.0f);

    Mtx trMtx;
    MR::makeMtxTR(trMtx, 0.0f, 0.0f, 0.0f, rotate.x, rotate.y, rotate.z);

    NebulaMtxMultVec(trMtx, dirZ, pVec);
}
