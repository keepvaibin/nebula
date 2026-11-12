#include "Game/Particle/ParticleResource.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Particle/ParticleDraw.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Util/MotionSignal.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Particle/ParticleShape.hpp"
#include "Game/Particle/ChildShape.hpp"
#include "Game/Particle/EmitterCore.hpp"
#include "Game/Particle/ExTexShape.hpp"
#include "Game/Particle/ExtraShape.hpp"
#include "Game/Particle/ParticleField.hpp"
#include "Game/Particle/ParticleKey.hpp"
#include "Game/Particle/ParticleCore.hpp"
#include "Game/Particle/ParticleResources.hpp"

ParticleResource::ParticleResource() {
    mpBaseShape = NULL;
    mpExtraShape = NULL;
    mpChildShape = NULL;
    mpExTexShape = NULL;
    mpDynamicsBlock = NULL;
    mpFieldBlocks = NULL;
    mpKeyBlocks = NULL;
    mpTDB1 = NULL;
    mKeyBlockNum = 0;
    mFieldBlockNum = 0;
    mUsrIdx = 0;
    mPlan = Plan();
}

namespace {
    u8 shapeOf(u32 type, bool rotate) {
        switch (type) {
        case 2:
            return rotate ? ParticleResource::Shape_RotBillboard : ParticleResource::Shape_Billboard;
        case 10:
            return rotate ? ParticleResource::Shape_RotYBillboard : ParticleResource::Shape_YBillboard;
        case 3:
        case 4:
            return rotate ? ParticleResource::Shape_RotDirection : ParticleResource::Shape_Direction;
        case 9:
            return ParticleResource::Shape_DBillboard;
        case 7:
        case 8:
            return ParticleResource::Shape_Rotation;
        case 0:
            return ParticleResource::Shape_Point;
        case 1:
            return ParticleResource::Shape_Line;
        default:
            return ParticleResource::Shape_None;
        }
    }

    // Anim of a shape's animation type (0 normal .. 4 random).
    u8 animOf(u32 type) {
        return type <= 4 ? static_cast< u8 >(type + 1) : ParticleResource::Anim_None;
    }

    // ScaleAnim of an extra shape's scale animation type (0 normal, 1 repeat, 2 reverse).
    u8 scaleAnimOf(u32 type) {
        return type <= 2 ? static_cast< u8 >(type + 1) : ParticleResource::ScaleAnim_None;
    }

    void drawShape(u8 shape, EmitterWork* pWork, ParticleCore* pParticle) {
        switch (shape) {
        case ParticleResource::Shape_Billboard:
            ParticleDrawBillboard(pWork, pParticle);
            break;
        case ParticleResource::Shape_RotBillboard:
            ParticleDrawRotBillboard(pWork, pParticle);
            break;
        case ParticleResource::Shape_YBillboard:
            ParticleDrawYBillboard(pWork, pParticle);
            break;
        case ParticleResource::Shape_RotYBillboard:
            ParticleDrawRotYBillboard(pWork, pParticle);
            break;
        case ParticleResource::Shape_Direction:
            ParticleDrawDirection(pWork, pParticle);
            break;
        case ParticleResource::Shape_RotDirection:
            ParticleDrawRotDirection(pWork, pParticle);
            break;
        case ParticleResource::Shape_DBillboard:
            ParticleDrawDBillboard(pWork, pParticle);
            break;
        case ParticleResource::Shape_Rotation:
            ParticleDrawRotation(pWork, pParticle);
            break;
        case ParticleResource::Shape_Point:
            ParticleDrawPoint(pWork, pParticle);
            break;
        case ParticleResource::Shape_Line:
            ParticleDrawLine(pWork, pParticle);
            break;
        }
    }

    void sizeOf(u8 size, EmitterWork* pWork, ParticleCore* pParticle) {
        switch (size) {
        case ParticleResource::Size_Point:
            ParticleSetPointSize(pWork, pParticle);
            break;
        case ParticleResource::Size_Line:
            ParticleSetLineWidth(pWork, pParticle);
            break;
        case ParticleResource::Size_TexCrdAnim:
            ParticleLoadCalcTexCrdMtxAnm(pWork, pParticle);
            break;
        }
    }

    void texIdx(u8 anim, EmitterWork* pWork) {
        switch (anim) {
        case ParticleResource::Anim_Normal:
            ParticleCalcTexIdxNormal(pWork);
            break;
        case ParticleResource::Anim_Repeat:
            ParticleCalcTexIdxRepeat(pWork);
            break;
        case ParticleResource::Anim_Reverse:
            ParticleCalcTexIdxReverse(pWork);
            break;
        case ParticleResource::Anim_Merge:
            ParticleCalcTexIdxMerge(pWork);
            break;
        case ParticleResource::Anim_Random:
            ParticleCalcTexIdxRandom(pWork);
            break;
        }
    }

    void texIdx(u8 anim, EmitterWork* pWork, ParticleCore* pParticle) {
        switch (anim) {
        case ParticleResource::Anim_Normal:
            ParticleCalcTexIdxNormal(pWork, pParticle);
            break;
        case ParticleResource::Anim_Repeat:
            ParticleCalcTexIdxRepeat(pWork, pParticle);
            break;
        case ParticleResource::Anim_Reverse:
            ParticleCalcTexIdxReverse(pWork, pParticle);
            break;
        case ParticleResource::Anim_Merge:
            ParticleCalcTexIdxMerge(pWork, pParticle);
            break;
        case ParticleResource::Anim_Random:
            ParticleCalcTexIdxRandom(pWork, pParticle);
            break;
        }
    }

    void clrIdx(u8 anim, EmitterWork* pWork) {
        switch (anim) {
        case ParticleResource::Anim_Normal:
            ParticleCalcClrIdxNormal(pWork);
            break;
        case ParticleResource::Anim_Repeat:
            ParticleCalcClrIdxRepeat(pWork);
            break;
        case ParticleResource::Anim_Reverse:
            ParticleCalcClrIdxReverse(pWork);
            break;
        case ParticleResource::Anim_Merge:
            ParticleCalcClrIdxMerge(pWork);
            break;
        case ParticleResource::Anim_Random:
            ParticleCalcClrIdxRandom(pWork);
            break;
        }
    }

    void clrIdx(u8 anim, EmitterWork* pWork, ParticleCore* pParticle) {
        switch (anim) {
        case ParticleResource::Anim_Normal:
            ParticleCalcClrIdxNormal(pWork, pParticle);
            break;
        case ParticleResource::Anim_Repeat:
            ParticleCalcClrIdxRepeat(pWork, pParticle);
            break;
        case ParticleResource::Anim_Reverse:
            ParticleCalcClrIdxReverse(pWork, pParticle);
            break;
        case ParticleResource::Anim_Merge:
            ParticleCalcClrIdxMerge(pWork, pParticle);
            break;
        case ParticleResource::Anim_Random:
            ParticleCalcClrIdxRandom(pWork, pParticle);
            break;
        }
    }

    void stripe(u8 kind, EmitterWork* pWork) {
        if (kind == ParticleResource::Stripe_Plain) {
            ParticleDrawStripe(pWork);
        } else if (kind == ParticleResource::Stripe_Cross) {
            ParticleDrawStripeX(pWork);
        }
    }
}  // namespace

void ParticleResource::init() {
    const bool is_glbl_clr_anm = mpBaseShape->isGlblClrAnm();
    const bool is_glbl_tex_anm = mpBaseShape->isGlblTexAnm();
    const bool is_prm_anm = mpBaseShape->isPrmAnm();
    const bool is_env_anm = mpBaseShape->isEnvAnm();
    const bool is_tex_anm = mpBaseShape->isTexAnm();
    const bool is_tex_crd_anm = mpBaseShape->isTexCrdAnm();
    const bool is_prj_tex = mpBaseShape->isPrjTex();
    const bool is_enable_scale_anm = mpExtraShape != NULL && mpExtraShape->isEnableScaleAnm();
    const bool is_enable_alpha_anm = mpExtraShape != NULL && mpExtraShape->isEnableAlphaAnm();
    const bool is_enable_alpha_flick = is_enable_alpha_anm && mpExtraShape->isEnableAlphaFlick();
    const bool is_enable_rotate_anm = mpExtraShape != NULL && mpExtraShape->isEnableRotateAnm();
    const bool is_rotate_on = is_enable_rotate_anm || (mpChildShape != NULL && mpChildShape->isRotateOn());
    const u32 base_type = mpBaseShape->getType();
    const bool base_type_5_6 = base_type == 5 || base_type == 6;
    const bool base_type_0_1 = base_type == 0 || base_type == 1;
    const bool child_type_5_6 = mpChildShape != NULL && (mpChildShape->getType() == 5 || mpChildShape->getType() == 6);
    const bool child_type_0_1 = mpChildShape != NULL && (mpChildShape->getType() == 0 || mpChildShape->getType() == 1);
    const bool is_draw_parent = !mpBaseShape->isNoDrawParent();
    const bool is_draw_child = !mpBaseShape->isNoDrawChild();
    Plan& p = mPlan;
    p = Plan();

    // Each emitter frame: the texture and colour indices and colours when the whole emitter shares them.
    if (is_glbl_tex_anm && is_tex_anm) {
        p.mEmitterTexIdx = animOf(mpBaseShape->getTexAnmType());
    }
    if (is_glbl_clr_anm) {
        p.mEmitterPrm = is_prm_anm;
        p.mEmitterEnv = is_env_anm;
        if (is_prm_anm || is_env_anm) {
            p.mEmitterClrIdx = animOf(mpBaseShape->getClrAnmType());
        }
    }

    // Each particle frame.
    if (!is_glbl_tex_anm && is_tex_anm) {
        p.mTexIdx = animOf(mpBaseShape->getTexAnmType());
    }
    if (!base_type_5_6 && is_enable_alpha_anm) {
        p.mAlpha = is_enable_alpha_flick ? Alpha_Flick : Alpha_Anim;
    }
    if (!is_glbl_clr_anm) {
        p.mPrm = is_prm_anm;
        p.mEnv = is_env_anm;
        if (is_prm_anm || is_env_anm) {
            p.mClrIdx = animOf(mpBaseShape->getClrAnmType());
        }
    } else {
        p.mColorCopy = true;
    }
    if (is_enable_scale_anm) {
        if (base_type != 0) {
            if (mpExtraShape->isScaleXYDiff()) {
                p.mScaleY = ScaleY_Own;
                if (mpExtraShape->getScaleAnmTypeY() != 0 || mpExtraShape->getScaleAnmTypeX() != 0) {
                    p.mScaleAnimY = scaleAnimOf(mpExtraShape->getScaleAnmTypeY());
                }
            } else {
                p.mScaleY = ScaleY_Copy;
            }
        }
        p.mScaleX = true;
        p.mScaleAnimX = scaleAnimOf(mpExtraShape->getScaleAnmTypeX());
    }
    p.mChildScaleOut = mpChildShape != NULL && mpChildShape->isScaleOutOn();
    p.mChildAlphaOut = mpChildShape != NULL && mpChildShape->isAlphaOutOn();

    // The emitter's draw.
    if (is_draw_parent && base_type_5_6) {
        p.mStripe = base_type == 5 ? Stripe_Plain : Stripe_Cross;
    }
    p.mExTex = mpExTexShape != NULL;
    if (!is_tex_anm) {
        p.mLoadTex = LoadTex_Fixed;
    } else if (is_glbl_tex_anm) {
        p.mLoadTex = LoadTex_Anim;
    }
    if (base_type_0_1) {
        p.mTexCrd = TexCrd_Identity;
    } else if (is_prj_tex) {
        p.mTexCrd = TexCrd_Projection;
    } else if (is_tex_crd_anm) {
        p.mTexCrd = base_type_5_6 ? TexCrd_CalcAnim : TexCrd_Anim;
    } else {
        p.mTexCrd = TexCrd_Identity;
    }
    p.mPosMtxCam = base_type_0_1;
    if (base_type_0_1 && !is_enable_scale_anm) {
        p.mSize = base_type == 0 ? Size_Point : Size_Line;
    }
    if (is_glbl_clr_anm) {
        p.mEmitterColor = base_type_5_6 || !is_enable_alpha_anm ? EmitterColor_PrmEnv : EmitterColor_Env;
    } else if (!is_prm_anm && !is_enable_alpha_anm) {
        p.mEmitterColor = !is_env_anm ? EmitterColor_PrmEnv : EmitterColor_Prm;
    } else if (!is_env_anm) {
        p.mEmitterColor = EmitterColor_Env;
    }

    // Each particle's draw.
    if (is_draw_parent && !base_type_5_6) {
        p.mShape = shapeOf(base_type, is_enable_rotate_anm);
    }
    p.mTexAnim = !is_glbl_tex_anm && is_tex_anm;
    if (base_type_0_1 && is_enable_scale_anm) {
        p.mParticleSize = base_type == 0 ? Size_Point : Size_Line;
    } else if (is_tex_crd_anm && !is_prj_tex) {
        p.mParticleSize = Size_TexCrdAnim;
    }
    if (!is_glbl_clr_anm) {
        if (is_prm_anm) {
            p.mParticleColor = is_env_anm ? ParticleColor_PrmAlphaEnv : ParticleColor_PrmAlpha;
        } else if (is_enable_alpha_anm) {
            p.mParticleColor = is_env_anm ? ParticleColor_AlphaEnv : ParticleColor_Alpha;
        } else if (is_env_anm) {
            p.mParticleColor = ParticleColor_Env;
        }
    } else if (is_enable_alpha_anm && !base_type_5_6) {
        p.mParticleColor = ParticleColor_Alpha;
    }

    // The children's draw.
    if (is_draw_child && child_type_5_6) {
        p.mChildStripe = mpChildShape->getType() == 5 ? Stripe_Plain : Stripe_Cross;
    }
    p.mChildPosMtxCam = child_type_0_1;
    p.mChildPrmEnv = mpChildShape != NULL && !mpChildShape->isAlphaOutOn() && !mpChildShape->isAlphaInherited() &&
                     !mpChildShape->isColorInherited();
    if (is_draw_child && mpChildShape != NULL && !child_type_5_6) {
        p.mChildShape = shapeOf(mpChildShape->getType(), is_rotate_on);
    }
    if (child_type_0_1) {
        p.mChildSize = mpChildShape->getType() == 0 ? Size_Point : Size_Line;
    }
    p.mChildPrmAlphaEnv = mpChildShape != NULL &&
                          (mpChildShape->isAlphaOutOn() || mpChildShape->isAlphaInherited() || mpChildShape->isColorInherited());
}

bool ParticleResource::calc(EmitterWork* work, EmitterCore* emtr) {
    work->mpEmtr = emtr;
    work->mpRes = this;
    work->mEmitCount = 0;

    if (!emtr->processTillStartFrame()) {
        return false;
    }

    if (emtr->processTermination()) {
        return true;
    }

    if (emtr->checkStatus(2)) {
        if (emtr->mpEmtrCallBack != NULL) {
            emtr->mpEmtrCallBack->execute(emtr);
            if (emtr->checkStatus(0x100)) {
                return true;
            }
            emtr->mpEmtrCallBack->executeAfter(emtr);
            if (emtr->checkStatus(0x100)) {
                return true;
            }
        }

    } else {
        calcKey(work);

        for (int i = mFieldBlockNum - 1; i >= 0; i--) {
            mpFieldBlocks[i]->initOpParam();
        }

        if (emtr->mpEmtrCallBack != NULL) {
            emtr->mpEmtrCallBack->execute(emtr);
            if (emtr->checkStatus(0x100)) {
                return true;
            }
        }

        calcWorkData_c(work);

        clrIdx(mPlan.mEmitterClrIdx, work);
        if (mPlan.mEmitterEnv) {
            ParticleCalcEnv(work);
        }
        if (mPlan.mEmitterPrm) {
            ParticleCalcPrm(work);
        }
        texIdx(mPlan.mEmitterTexIdx, work);

        for (int i = mFieldBlockNum - 1; i >= 0; i--) {
            mpFieldBlocks[i]->prepare(work);
        }

        if (!emtr->checkStatus(8)) {
            mpDynamicsBlock->create(work);
        }

        if (emtr->mpEmtrCallBack != NULL) {
            emtr->mpEmtrCallBack->executeAfter(emtr);
            if (emtr->checkStatus(0x100)) {
                return true;
            }
        }

        ParticleSlot* node = emtr->mAlivePtclBase.getFirst();
        ParticleSlot* next;
        while (node != emtr->mAlivePtclBase.getEnd()) {
            next = node->getNext();
            if (node->getObject()->calc_p(work)) {
                emtr->mPtclPool.push_front(emtr->mAlivePtclBase.erase(node));
            }
            node = next;
        }

        ParticleSlot* child = emtr->mAlivePtclChld.getFirst();
        while (child != emtr->mAlivePtclChld.getEnd()) {
            ParticleSlot* childNext = child->getNext();
            if (child->getObject()->calc_c(work)) {
                emtr->mPtclPool.push_front(emtr->mAlivePtclChld.erase(child));
            }
            child = childNext;
        }

        emtr->mTick++;
    }

    return false;
}

void ParticleResource::draw(EmitterWork* work, EmitterCore* emtr) {
    work->mpEmtr = emtr;
    work->mpRes = this;
    work->mDrawCount = 0;
    calcWorkData_d(work);
    mpBaseShape->setPass(work);
    for (s32 i = 1; i <= emtr->getDrawTimes(); i++) {
        work->mDrawCount++;
        if (mpBaseShape->isDrawPrntAhead() && mpChildShape != NULL)
            drawC(work);
        drawP(work);
        if (!mpBaseShape->isDrawPrntAhead() && mpChildShape != NULL)
            drawC(work);
    }
}

void ParticleResource::drawP(EmitterWork* work) {
    ParticleDraw::setSource(MotionSignal::drawEmitter(this, work->mpEmtr, false));
    work->mpEmtr->clearStatus(0x80);

    work->mGlobalPtclScl.x = work->mpEmtr->mGlobalPScl.x * mpBaseShape->getBaseSizeX();
    work->mGlobalPtclScl.y = work->mpEmtr->mGlobalPScl.y * mpBaseShape->getBaseSizeY();

    if (mpBaseShape->getType() == 0) {
        work->mGlobalPtclScl.x *= 1.02f;
    } else if (mpBaseShape->getType() == 1) {
        work->mGlobalPtclScl.x *= 1.02f;
        work->mGlobalPtclScl.y *= 0.4f;
    }

    if (mpExtraShape != NULL && mpExtraShape->isEnableScaleAnm()) {
        work->mPivot.x = mpExtraShape->getScaleCenterX() - 1.0f;
        work->mPivot.y = mpExtraShape->getScaleCenterY() - 1.0f;
    } else {
        work->mPivot.x = work->mPivot.y = 0.0f;
    }

    work->mDirType = mpBaseShape->getDirType();
    work->mRotType = mpBaseShape->getRotType();
    work->mListType = mpBaseShape->getType() == 4 || mpBaseShape->getType() == 8;
    work->mPlaneType = work->mListType ? 2 : mpBaseShape->getBasePlaneType();
    work->mPrjType = mpBaseShape->isPrjTex() ? (mpBaseShape->isTexCrdAnm() ? 2 : 1) : 0;

    work->mpAlivePtcl = &work->mpEmtr->mAlivePtclBase;
    setPTev();

    switch (mPlan.mEmitterColor) {
    case EmitterColor_PrmEnv:
        ParticleRegistPrmEnv(work);
        break;
    case EmitterColor_Env:
        ParticleRegistEnv(work);
        break;
    case EmitterColor_Prm:
        ParticleRegistPrm(work);
        break;
    }
    if (mPlan.mSize == Size_Point) {
        ParticleSetPointSize(work);
    } else if (mPlan.mSize == Size_Line) {
        ParticleSetLineWidth(work);
    }
    if (mPlan.mPosMtxCam) {
        ParticleLoadPosMtxCam(work);
    }
    switch (mPlan.mTexCrd) {
    case TexCrd_Identity:
        ParticleGenTexCrdMtxIdt(work);
        break;
    case TexCrd_Projection:
        ParticleGenTexCrdMtxPrj(work);
        break;
    case TexCrd_CalcAnim:
        ParticleGenCalcTexCrdMtxAnm(work);
        break;
    case TexCrd_Anim:
        ParticleGenTexCrdMtxAnm(work);
        break;
    }
    if (mPlan.mLoadTex == LoadTex_Fixed) {
        ParticleLoadTex(work);
    } else if (mPlan.mLoadTex == LoadTex_Anim) {
        ParticleLoadTexAnm(work);
    }
    if (mPlan.mExTex) {
        ParticleLoadExTex(work);
    }
    ParticleDrawEmitterCallBackB(work);
    stripe(mPlan.mStripe, work);

    if (mpBaseShape->isDrawFwdAhead()) {
        ParticleSlot* node = work->mpEmtr->mAlivePtclBase.getLast();
        for (; node != work->mpEmtr->mAlivePtclBase.getEnd(); node = node->getPrev()) {
            work->mpCurNode = node;
            ParticleDraw::particle(node->getObject());
            drawParticle(work, node->getObject());
        }
    } else {
        ParticleSlot* node = work->mpEmtr->mAlivePtclBase.getFirst();
        for (; node != work->mpEmtr->mAlivePtclBase.getEnd(); node = node->getNext()) {
            work->mpCurNode = node;
            ParticleDraw::particle(node->getObject());
            drawParticle(work, node->getObject());
        }
    }


    ParticleDraw::flush();

    if (work->mpEmtr->mpEmtrCallBack != NULL) {
        work->mpEmtr->mpEmtrCallBack->drawAfter(work->mpEmtr);
    }
}

void ParticleResource::drawC(EmitterWork* work) {
    ParticleDraw::setSource(MotionSignal::drawEmitter(this, work->mpEmtr, true));
    work->mpEmtr->setStatus(0x80);

    if (mpChildShape->isScaleInherited()) {
        work->mGlobalPtclScl.x = work->mpEmtr->mGlobalPScl.x * mpBaseShape->getBaseSizeX();
        work->mGlobalPtclScl.y = work->mpEmtr->mGlobalPScl.y * mpBaseShape->getBaseSizeY();
    } else {
        work->mGlobalPtclScl.x = work->mpEmtr->mGlobalPScl.x * mpChildShape->getScaleX();
        work->mGlobalPtclScl.y = work->mpEmtr->mGlobalPScl.y * mpChildShape->getScaleY();
    }

    if (mpChildShape->getType() == 0) {
        work->mGlobalPtclScl.x *= 1.02f;
    } else if (mpChildShape->getType() == 1) {
        work->mGlobalPtclScl.x *= 1.02f;
        work->mGlobalPtclScl.y *= 0.4f;
    }

    work->mPivot.x = work->mPivot.y = 0.0f;

    work->mDirType = mpChildShape->getDirType();
    work->mRotType = mpChildShape->getRotType();
    work->mListType = mpChildShape->getType() == 4 || mpChildShape->getType() == 8;
    work->mPlaneType = work->mListType ? 2 : mpChildShape->getBasePlaneType();
    work->mPrjType = 0;

    work->mpAlivePtcl = &work->mpEmtr->mAlivePtclChld;
    setCTev(work);

    if (mPlan.mChildPrmEnv) {
        ParticleRegistChildPrmEnv(work);
    }
    if (mPlan.mChildPosMtxCam) {
        ParticleLoadPosMtxCam(work);
    }
    ParticleDrawEmitterCallBackB(work);
    stripe(mPlan.mChildStripe, work);

    if (mpBaseShape->isDrawFwdAhead()) {
        ParticleSlot* node = work->mpEmtr->mAlivePtclChld.getLast();
        for (; node != work->mpEmtr->mAlivePtclChld.getEnd(); node = node->getPrev()) {
            work->mpCurNode = node;
            ParticleDraw::particle(node->getObject());
            drawChild(work, node->getObject());
        }
    } else {
        ParticleSlot* node = work->mpEmtr->mAlivePtclChld.getFirst();
        for (; node != work->mpEmtr->mAlivePtclChld.getEnd(); node = node->getNext()) {
            work->mpCurNode = node;
            ParticleDraw::particle(node->getObject());
            drawChild(work, node->getObject());
        }
    }


    if (work->mpEmtr->mpEmtrCallBack != NULL) {
        work->mpEmtr->mpEmtrCallBack->drawAfter(work->mpEmtr);
    }

    ParticleDraw::flush();
}

namespace {
    // runtime/src/render/materials/particle.hlsl for the emitter's draws: the base shape's modes and alpha test, the
    // textures the emitter's functions chose, the blending, depth and culling the
    // shape and the manager set.
    // The particle material in use and the colours the emitter and particle functions set (ParticleMaterial): prm and
    // env carry over from one emitter to the next and travel on the particles' instances and corners.
    SiteMaterial sParticle;
    bool sParticleSent = false;
    // The textures the emitter's functions chose, by slot.
    const SiteTexture* sParticleTextures[4] = {nullptr, nullptr, nullptr, nullptr};
    NebulaColor8 sPrm = {0, 0, 0, 0};
    NebulaColor8 sEnv = {0, 0, 0, 0};
    // The indirect matrix the emitter's extra texture shape gives (its rows scaled by 2^exponent), or none.
    f32 sIndirect[2][4];

    // The material with its own values: the indirect matrix (rows 0-1), the alpha test's references. prm and env are
    // not in its constants (the colour registers stay zero): the shader reads them from each particle's instance or
    // corner, and sPrm and sEnv carry over from the emitter drawn before, so they would only make the rows change from
    // frame to frame with nothing reading them.
    void sendParticle() {
        SiteMaterial& rMaterial = sParticle;
        for (u32 row = 0; row < 2; row++) {
            for (u32 c = 0; c < 4; c++) {
                rMaterial.constants[48 + row * 4 + c] = sIndirect[row][c];
            }
        }
        rMaterial.constants[80] = rMaterial.constants[88 + 4] / 255.0f;
        rMaterial.constants[81] = rMaterial.constants[88 + 5] / 255.0f;
    }

    // runtime/src/render/materials/particle.hlsl for the emitter's draws: the base shape's modes and alpha test, the
    // textures as the emitter's functions load them (the drawing state's maps), the blending, depth and culling the
    // shape and the manager set; its colours from ParticleMaterial. `place` and `coord`: the first corner place and
    // coordinate of the draws' quad in particle_instances.hlsl's tables.
    void sendParticleMaterial(const ParticleShape* pShape, u32 flags, u32 place, u32 coord) {
        ParticleDraw::begin((flags & 7) != 0);
        SiteMaterial& material = sParticle;
        material.init(NEBULA_SHADER_PARTICLE, SiteMaterial::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        material.mState.blend_op = pShape->getBlendMode();
        material.mState.blend_src = pShape->getBlendSrc();
        material.mState.blend_dst = pShape->getBlendDst();
        material.mState.depth_test = pShape->getZEnable();
        material.mState.depth_func = pShape->getZCmp();
        material.mState.depth_write = pShape->getZUpd();
        material.constants[84] = static_cast< f32 >(pShape->getStageColorMode());
        material.constants[85] = static_cast< f32 >(pShape->getStageAlphaMode());
        material.constants[86] = static_cast< f32 >(flags);
        material.constants[87] = static_cast< f32 >(place);
        material.constants[88] = static_cast< f32 >(pShape->getAlphaCmp0());
        material.constants[89] = static_cast< f32 >(pShape->getAlphaCmp1());
        material.constants[90] = static_cast< f32 >(pShape->getAlphaOp());
        material.constants[91] = static_cast< f32 >(coord);
        material.mState.pixel_shading = nebula_alpha_test_always(pShape->getAlphaCmp0(), pShape->getAlphaCmp1(),
                                                                pShape->getAlphaOp()) != 0;
        // The references (whole values; sendParticle scales them into the alpha test's slots).
        material.constants[92] = static_cast< f32 >(pShape->getAlphaRef0());
        material.constants[93] = static_cast< f32 >(pShape->getAlphaRef1());
        for (u32 slot = 0; slot < 4; slot++) {
            if (sParticleTextures[slot] != nullptr) {
                material.setTexture(slot, *sParticleTextures[slot]);
            }
        }
        sParticleSent = true;
        sendParticle();
    }

    void setIndirect(const f32 (*pMtx)[3], s8 scaleExp) {
        f32 scale = 1.0f;
        for (s32 e = scaleExp; e > 0; e--) {
            scale *= 2.0f;
        }
        for (s32 e = scaleExp; e < 0; e++) {
            scale *= 0.5f;
        }
        for (u32 row = 0; row < 2; row++) {
            for (u32 c = 0; c < 3; c++) {
                sIndirect[row][c] = pMtx[row][c] * scale;
            }
            sIndirect[row][3] = 0.0f;
        }
    }

}  // namespace

namespace ParticleMaterial {
    const SiteMaterial& material() {
        return sParticle;
    }

    void lineWidth(f32 pixels) {
        sParticle.mState.line_width = pixels;
        sParticle.mState.spread_lines = true;
    }

    void pointSize(f32 pixels) {
        sParticle.mState.point_size = pixels;
        sParticle.mState.spread_points = true;
    }

    void texture(u32 slot, const SiteTexture* pTexture) {
        if (sParticleTextures[slot] == pTexture) {
            return;
        }
        ParticleDraw::flush();
        sParticleTextures[slot] = pTexture;
        sParticle.setTexture(slot, *pTexture);
        if (sParticleSent) {
            sendParticle();
        }
    }

    u32 prmWord() {
        return (static_cast< u32 >(sPrm.r) << 24) | (static_cast< u32 >(sPrm.g) << 16) | (static_cast< u32 >(sPrm.b) << 8) | sPrm.a;
    }
    u32 envWord() {
        return (static_cast< u32 >(sEnv.r) << 24) | (static_cast< u32 >(sEnv.g) << 16) | (static_cast< u32 >(sEnv.b) << 8) | sEnv.a;
    }
    void prm(const NebulaColor8& rColor) {
        sPrm = rColor;
    }

    void env(const NebulaColor8& rColor) {
        sEnv = rColor;
    }

    void prmEnv(const NebulaColor8& rPrm, const NebulaColor8& rEnv) {
        sPrm = rPrm;
        sEnv = rEnv;
    }
}  // namespace ParticleMaterial

void ParticleResource::setPTev() {
    u32 flags = 0;
    int base_plane_type = (mpBaseShape->getType() == 3 || mpBaseShape->getType() == 7) ? mpBaseShape->getBasePlaneType() : 0;
    int center_offset;
    if (mpExtraShape != NULL) {
        center_offset = (mpExtraShape->getScaleCenterX() + 3 * mpExtraShape->getScaleCenterY()) * 0xC;
    } else {
        center_offset = 0x30;
    }
    const u32 place = (center_offset + base_plane_type * 0x6C) / 3;
    const u32 coord = (mpBaseShape->getTilingS() + 2 * mpBaseShape->getTilingT()) * 4;

    if (mpExTexShape != NULL) {
        if (mpExTexShape->isUseIndirect()) {
            f32 indTexMtx[2][3];
            mpExTexShape->getIndTexMtx(indTexMtx);
            setIndirect(indTexMtx, mpExTexShape->getExpScale());
            flags |= 1;
        }
        if (mpExTexShape->isUseSecTex()) {
            flags |= 2;
        }
    }

    if (mpBaseShape->isPrjTex()) {
        flags |= 4;
    }
    sendParticleMaterial(mpBaseShape, flags, place, coord);
}

void ParticleResource::setCTev(EmitterWork* work) {
    int base_plane_type = (mpChildShape->getType() == 3 || mpChildShape->getType() == 7) ? mpChildShape->getBasePlaneType() : 0;
    const u32 place = (0x30 + base_plane_type * 0x6C) / 3;
    work->mpResMgr->use(work->mpRes->getTexIdx(mpChildShape->getTexIdx()), 1);
    sendParticleMaterial(mpBaseShape, 8, place, 0);
}

// Each particle frame, last chosen first: scale, colour, alpha, texture index.
void ParticleResource::calc_p(EmitterWork* work, ParticleCore* ptcl) {
    const Plan& p = mPlan;
    switch (p.mScaleAnimX) {
    case ScaleAnim_Normal:
        ParticleCalcScaleAnmNormal(work, ptcl);
        break;
    case ScaleAnim_Repeat:
        ParticleCalcScaleAnmRepeatX(work, ptcl);
        break;
    case ScaleAnim_Reverse:
        ParticleCalcScaleAnmReverseX(work, ptcl);
        break;
    }
    if (p.mScaleX) {
        ParticleCalcScaleX(work, ptcl);
    }
    switch (p.mScaleAnimY) {
    case ScaleAnim_Normal:
        ParticleCalcScaleAnmNormal(work, ptcl);
        break;
    case ScaleAnim_Repeat:
        ParticleCalcScaleAnmRepeatY(work, ptcl);
        break;
    case ScaleAnim_Reverse:
        ParticleCalcScaleAnmReverseY(work, ptcl);
        break;
    }
    if (p.mScaleY == ScaleY_Own) {
        ParticleCalcScaleY(work, ptcl);
    } else if (p.mScaleY == ScaleY_Copy) {
        ParticleCalcScaleCopy(work, ptcl);
    }
    if (p.mColorCopy) {
        ParticleCalcColorCopy(work, ptcl);
    }
    clrIdx(p.mClrIdx, work, ptcl);
    if (p.mEnv) {
        ParticleCalcEnv(work, ptcl);
    }
    if (p.mPrm) {
        ParticleCalcPrm(work, ptcl);
    }
    if (p.mAlpha == Alpha_Flick) {
        ParticleCalcAlphaFlickAnm(work, ptcl);
    } else if (p.mAlpha == Alpha_Anim) {
        ParticleCalcAlphaAnm(work, ptcl);
    }
    texIdx(p.mTexIdx, work, ptcl);
}

void ParticleResource::calc_c(EmitterWork* work, ParticleCore* ptcl) {
    if (mPlan.mChildAlphaOut) {
        ParticleCalcChildAlphaOut(work, ptcl);
    }
    if (mPlan.mChildScaleOut) {
        ParticleCalcChildScaleOut(work, ptcl);
    }
}

// Each particle's draw, last chosen first: colour, size or texture coordinates, texture, the callback, the shape.
void ParticleResource::drawParticle(EmitterWork* work, ParticleCore* ptcl) {
    switch (mPlan.mParticleColor) {
    case ParticleColor_PrmAlphaEnv:
        ParticleRegistPrmAlphaEnv(work, ptcl);
        break;
    case ParticleColor_PrmAlpha:
        ParticleRegistPrmAlpha(work, ptcl);
        break;
    case ParticleColor_AlphaEnv:
        ParticleRegistAlphaEnv(work, ptcl);
        break;
    case ParticleColor_Alpha:
        ParticleRegistAlpha(work, ptcl);
        break;
    case ParticleColor_Env:
        ParticleRegistEnv(work, ptcl);
        break;
    }
    sizeOf(mPlan.mParticleSize, work, ptcl);
    if (mPlan.mTexAnim) {
        ParticleLoadTexAnm(work, ptcl);
    }
    ParticleDrawParticleCallBack(work, ptcl);
    drawShape(mPlan.mShape, work, ptcl);
}

void ParticleResource::drawChild(EmitterWork* work, ParticleCore* ptcl) {
    if (mPlan.mChildPrmAlphaEnv) {
        ParticleRegistPrmAlphaEnv(work, ptcl);
    }
    sizeOf(mPlan.mChildSize, work, ptcl);
    ParticleDrawParticleCallBack(work, ptcl);
    drawShape(mPlan.mChildShape, work, ptcl);
}

void ParticleResource::calcField(EmitterWork* work, ParticleCore* ptcl) {
    for (int i = mFieldBlockNum - 1; i >= 0; i--) {
        mpFieldBlocks[i]->calc(work, ptcl);
    }
}

void ParticleResource::calcKey(EmitterWork* work) {
    for (int i = mKeyBlockNum - 1; i >= 0; i--) {
        f32 keyValue = mpKeyBlocks[i]->calc(work->mpEmtr->mTick);
        switch (mpKeyBlocks[i]->getID()) {
        case ParticleKey::Target_Rate:
            work->mpEmtr->mRate = keyValue;
            break;
        case ParticleKey::Target_VolumeSize:
            work->mpEmtr->mVolumeSize = (s32)(keyValue);
            break;
        case ParticleKey::Target_VolumeMinRad:
            work->mpEmtr->mVolumeMinRad = keyValue;
            break;
        case ParticleKey::Target_LifeTime:
            work->mpEmtr->mLifeTime = (s32)(keyValue);
            break;
        case ParticleKey::Target_AwayFromCenterSpeed:
            work->mpEmtr->mAwayFromCenterSpeed = keyValue;
            break;
        case ParticleKey::Target_AwayFromAxisSpeed:
            work->mpEmtr->mAwayFromAxisSpeed = keyValue;
            break;
        case ParticleKey::Target_DirSpeed:
            work->mpEmtr->mDirSpeed = keyValue;
            break;
        case ParticleKey::Target_Spread:
            work->mpEmtr->mSpread = keyValue;
            break;
        case ParticleKey::Target_ScaleOut:
            work->mpEmtr->mScaleOut = keyValue;
            break;
        }
    }
}

void ParticleResource::calcWorkData_c(EmitterWork* work) {
    work->mVolumeSize = work->mpEmtr->mVolumeSize;
    work->mVolumeMinRad = work->mpEmtr->mVolumeMinRad;
    work->mVolumeSweep = work->mpEmtr->mVolumeSweep;
    work->mVolumeX = 0;
    work->mVolumeAngleNum = 0;
    work->mVolumeAngleMax = 1;
    work->mDivNumber = mpDynamicsBlock->getDivNumber() * 2 + 1;
    Mtx local_scl_mtx, local_rot_mtx, global_mtx;
    NebulaMtxScale(local_scl_mtx, work->mpEmtr->mLocalScl.x, work->mpEmtr->mLocalScl.y, work->mpEmtr->mLocalScl.z);
    ParticleMath::xyzRotateMtx(work->mpEmtr->mLocalRot.x * 182, work->mpEmtr->mLocalRot.y * 182, work->mpEmtr->mLocalRot.z * 182, local_rot_mtx);
    NebulaMtxScale(global_mtx, work->mpEmtr->mGlobalScl.x, work->mpEmtr->mGlobalScl.y, work->mpEmtr->mGlobalScl.z);
    NebulaMtxConcat(work->mpEmtr->mGlobalRot, global_mtx, global_mtx);
    global_mtx[0][3] = work->mpEmtr->mGlobalTrs.x;
    global_mtx[1][3] = work->mpEmtr->mGlobalTrs.y;
    global_mtx[2][3] = work->mpEmtr->mGlobalTrs.z;
    NebulaMtxCopy(work->mpEmtr->mGlobalRot, work->mRotationMtx);
    NebulaMtxConcat(work->mRotationMtx, local_rot_mtx, work->mGlobalRot);
    NebulaMtxConcat(work->mGlobalRot, local_scl_mtx, work->mGlobalSR);
    work->mEmitterPos.set(work->mpEmtr->mLocalTrs);
    work->mGlobalScl.mul(work->mpEmtr->mGlobalScl, work->mpEmtr->mLocalScl);
    ParticleMath::dirMtx(work->mpEmtr->mLocalDir, work->mDirectionMtx);
    work->mPublicScale.set(work->mpEmtr->mGlobalScl);
    NebulaMtxMultVec(global_mtx, &work->mpEmtr->mLocalTrs, &work->mGlobalPos);
}

void ParticleResource::calcWorkData_d(EmitterWork* work) {
    Mtx mtx;
    ParticleMath::xyzRotateMtx(work->mpEmtr->mLocalRot.x * 0xB6, work->mpEmtr->mLocalRot.y * 0xB6, work->mpEmtr->mLocalRot.z * 0xB6, mtx);
    NebulaMtxConcat(work->mpEmtr->mGlobalRot, mtx, work->mGlobalRot);
    NebulaMtxMultVecSR(work->mGlobalRot, &work->mpEmtr->mLocalDir, &work->mGlobalEmtrDir);
}

