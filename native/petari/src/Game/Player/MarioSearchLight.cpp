#include "Game/Player/MarioSearchLight.hpp"
#include "Game/Util/NativeShader.hpp"
#include <JSystem/J3DGraphBase/J3DNativeMaterial.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Player/DrawAdaptor.hpp"
#include "Game/Player/J3DModelX.hpp"
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
#include <JSystem/JGeometry/TVec.hpp>
#include "Game/System/NebulaArena.hpp"
#include <nebula_math.h>

MarioSearchLight::MarioSearchLight(LiveActor* pActor) : PartsModel(pActor, "\203\124\201\133\203\140\203\211\203\103\203\147" /* サーチライト */, "SearchLightCone", nullptr, -1, 0), _9C(), _A0(), _A4() {
}

void MarioSearchLight::init(const JMapInfoIter& rParam1) {
    PartsModel::init(rParam1);
    initHitSensor(1);
    _9C = 0.0f;
    _A4 = nullptr;
    _A0 = 0;
    MR::addHitSensorCallback(this, "eye", ATYPE_EYE, 4, 100.0f);
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

    if (pReceiver != _A4) {
        MR::tryRumblePadWeak(this, PLAYER_1);
    }

    _A4 = pReceiver;
    _A0 = 30;
}

void MarioSearchLight::updateHitSensor(HitSensor* pSensor) {
    TVec3f frontVec;
    MR::calcFrontVec(&frontVec, this);

    TVec3f frontPosition = mPosition + frontVec * _9C;

    pSensor->mPosition.x = frontPosition.x;
    pSensor->mPosition.y = frontPosition.y;
    pSensor->mPosition.z = frontPosition.z;

    _9C += 80.0f;
    if (_9C > 2000.0f) {
        _9C = 0.0f;
    }

    if (_A4 != nullptr) {
        if (!_A4->mValidByHost) {
            _A4 = nullptr;
        }
    }

    if (_A0 != 0) {
        _A0--;
    }

    if (_A0 == 0) {
        _A4 = nullptr;
    }
}

void MarioSearchLight::calcAndSetBaseMtx() {
    Mtx baseMtx;
    NebulaMtxCopy(getBaseMtx(), baseMtx);

    PartsModel::calcAndSetBaseMtx();
    MR::blendMtxRotateSlerp(baseMtx, getBaseMtx(), 0.1f, baseMtx);

    TVec3f trans;
    MR::extractMtxTrans(getBaseMtx(), &trans);
    MR::setMtxTrans(baseMtx, trans.x, trans.y, trans.z);
    MR::setBaseTRMtx(this, baseMtx);
}

void MarioActor::initThrowing() {
    mSearchLightThrowPos = new FixedPosition(this, "HandR", TVec3f(13.19f, 41.57f, 19.02f), TVec3f(-3.63f, -41.69f, 15.76f));
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
    inline J3DModelX* getModelX(LiveActor* pActor) {
        return static_cast< J3DModelX* >(MR::getJ3DModel(pActor));
    }

    inline void initExtraMtxBuffer(J3DModelX* pModel, s32 num) {
        for (u32 i = 0; i < pModel->_DD; i++) {
            pModel->mExtraMtxBuffer[i] = new (0x20) Mtx[num];
        }
    }
};  // namespace

void MarioActor::initSearchLight() {
    mSearchLight = new MarioSearchLight(this);
    mSearchLight->initWithoutIter();
    mSearchLight->makeActorDead();
    J3DModelX* model = static_cast< J3DModelX* >(MR::getJ3DModel(mSearchLight));
    model->_DD = 16;

    s32 drawMtxNum = model->getModelData()->getDrawMtxNum();
    ::initExtraMtxBuffer(model, drawMtxNum);

    mDrawSearchLight = new DrawAdaptor(MR::Functor(this, &MarioActor::drawSearchLight), MR::DrawType_0x33);
}

void MarioActor::updateThrowing() {
    if (mSearchLightThrowPos == nullptr) {
        return;
    }

    mSearchLightThrowPos->calc();

    if (mSearchLight == nullptr) {
        return;
    }

    TVec3f trans;
    mSearchLightThrowPos->mMtx.getTrans(trans);

    TVec3f& position = mSearchLight->mPosition;
    position = trans + mVelocity;

    mSearchLightThrowPos->copyRotate(&mSearchLight->mRotation);

    MR::calcAnimDirect(mSearchLight);
}

void MarioActor::calcViewSearchLight() {
    if (mSearchLight == nullptr) {
        return;
    }

    if (!MR::isDead(mSearchLight)) {
        J3DModelX* model = ::getModelX(mSearchLight);

        // The search light is has three scaled cone "shells" to fake a blur effect.
        Mtx modelMdx;
        MR::multMtx(modelMdx, model->getBaseTRMtx(), j3dSys.getViewMtx());

        Mtx invBaseMtx;
        NebulaMtxInverse(model->getBaseTRMtx(), invBaseMtx);

        model->viewCalc3(0, nullptr);

        for (u32 i = 0; i < ARRAY_SIZE(mConst->getTable()->mSearchLightBlurScale); i++) {
            Mtx scaledMtx;
            f32 scale = mConst->getTable()->mSearchLightBlurScale[i];
            NebulaMtxScale(scaledMtx, scale, scale, scale);

            MR::multMtx(scaledMtx, scaledMtx, modelMdx);
            MR::multMtx(j3dSys.getViewMtx(), invBaseMtx, scaledMtx);

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

    TVec3f throwVec;
    getThrowVec(&throwVec);

    TVec2f screenPos1;
    TDDraw::project2D(&screenPos1, mPosition);

    TVec2f screenPos2;
    TDDraw::project2D(&screenPos2, mPosition + throwVec * 10000.0f);

    TVec2f minimum;
    TVec2f maximum;
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

    screenPos1 = minimum + TVec2f(-10.0f, -10.0f);
    screenPos2 = maximum + TVec2f(10.0f, 10.0f);

    // The light's screen box cleared in the alpha buffer (vertex_color, alpha only, a constant destination alpha of 0).
    NebulaNativeMaterial box;
    TDDraw::setupNative(&box, 0, 0, 2);
    box.color_write = 0;
    box.alpha_write = 1;
    NativeShader::setBlend(box, NEBULA_FACTOR_DST_ALPHA, NEBULA_FACTOR_ONE);
    J3DPass::constantAlpha(true, 0);
    NativeShader::send(box);

    TDDraw::drawFillBox(screenPos1, screenPos2, 0);
    // The light model with its own material and the passes over it (J3DNativeMaterial::override); setup's view only.
    NebulaNativeMaterial view;
    TDDraw::setupNative(&view, 0, 1, 1);

    J3DModelX* model = static_cast< J3DModelX* >(MR::getJ3DModel(mSearchLight));
    J3DMaterial* const material = model->getModelData()->getMaterialNodePointer(0);
    if (model->simpleDrawSetup(material)) {
        J3DPass::depth(true, NEBULA_COMPARE_LEQUAL, false);
        J3DPass::writeAlpha(true);
        J3DPass::writeColor(false);
        J3DPass::constantAlpha(false, 0);

        // Gradually fade in the search light.
        f32 lightBrightness = 1.0f;
        if (mSearchLightTimer < 30) {
            lightBrightness = 0.0f;
        } else if (mSearchLightTimer < 180) {
            lightBrightness = (mSearchLightTimer - 30) / 150.0f;
        }

        // The channel colour at alpha A0.
        const u8 volumeAlpha = (u8)(s32)((lightBrightness * mConst->getTable()->mSearchLightColor[3]));
        // (The light's model is a native J3D material: J3DNativeMaterial::override draws the pass over it.)
        J3DNativeMaterial::override(2, volumeAlpha / 255.0f, 0.0f, -1.0f);

        // Render intersection of light cone with the scene by subtracting
        // visible back faces from front faces.
        J3DPass::blend(NEBULA_FACTOR_ONE, NEBULA_FACTOR_ONE);
        J3DPass::cull(NEBULA_CULL_BACK);
        model->setDrawView(0);
        model->simpleDrawShape(material);

        J3DPass::cull(NEBULA_CULL_FRONT);
        J3DPass::subtractBlend();
        model->simpleDrawShape(material);

        // The light's colour added over the box by the destination alpha the volume left (vertex_color).
        NebulaNativeMaterial fill;
        TDDraw::setupNative(&fill, 0, 1, 2);
        fill.alpha_write = 1;
        NativeShader::setBlend(fill, NEBULA_FACTOR_DST_ALPHA, NEBULA_FACTOR_ONE);
        J3DPass::constantAlpha(true, 0);
        NativeShader::send(fill);

        u8* col = mConst->getTable()->mSearchLightColor;
        TDDraw::drawFillBox(screenPos1, screenPos2, col[0] << 24 | col[1] << 16 | col[2] << 8);

        J3DPass::constantAlpha(false, 0);
        J3DPass::writeAlpha(false);

        TDDraw::setupNative(&view, 0, 1, 1);
        J3DPass::depth(true, NEBULA_COMPARE_LEQUAL, false);
        bool res = model->simpleDrawSetup(material);

        J3DPass::blend(NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_ONE);

        // Colour of the light cone: the channel colour at alpha RASA - K(1 - A0), drawn above 4.
        const u8 coneAlpha = (u8)(s32)((lightBrightness * mConst->getTable()->mSearchLightAlphaLevel));
        J3DNativeMaterial::override(3, coneAlpha / 255.0f, 0.0f, 4.0f);

        if (res) {
            // Draw central light cone.
            model->simpleDrawShape(material);
        } else {
            return;
        }

        // Draw soft falloff outside light cone.
        for (u32 i = 0; i < ARRAY_SIZE(mConst->getTable()->mSearchLightBlurAlpha); i++) {
            model->setDrawView(i + 1);

            // Cone falloff colour (A0) and end cap falloff colour (A1): the channel colour at (A1 + (A0 - A1) RASA) / 2.
            const u8 blurAlpha = (u8)(s32)((lightBrightness * mConst->getTable()->mSearchLightBlurAlpha[i]));
            J3DNativeMaterial::override(4, blurAlpha / 255.0f, 4.0f / 255.0f, 4.0f);

            model->simpleDrawShape(material);
        }
    }
}

void MarioActor::getThrowVec(TVec3f* pVec) const {
    TVec3f rotate;
    mSearchLightThrowPos->copyRotate(&rotate);

    TVec3f dirZ(0.0f, 0.0f, 1.0f);

    Mtx trMtx;
    MR::makeMtxTR(trMtx, 0.0f, 0.0f, 0.0f, rotate.x, rotate.y, rotate.z);

    NebulaMtxMultVec(trMtx, dirZ, pVec);
}
