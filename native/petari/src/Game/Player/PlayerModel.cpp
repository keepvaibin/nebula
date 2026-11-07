#include "Game/Player/PlayerModel.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/RenderTexture.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>

void PlayerModel::viewCalc2() {
    capturePose(&mPose, mAnmMtx, mModelData->getJointCount(), nullptr, mBaseTransformMtx);
    NebulaMtxCopy(ModelSys::mViewMtx, mViewAtCalc);
    mDrawPose = &mPose;
    mNormalPose = &mPose;
}

void PlayerModel::setDrawView(u32 view) {
    mDrawPose = mExtraPoses[view + _DC];
}

void PlayerModel::copyAnmMtxBuffer(const PlayerModel* pModel) {
    mAnmMtx = pModel->mAnmMtx;
}

void PlayerModel::viewCalc3(u32 view, const f32 (*pMtx)[4]) {
    NebulaMtxCopy(ModelSys::mViewMtx, mViewAtCalc);
    ModelPose* pPose = mExtraPoses[view + _DC];
    capturePose(pPose, mAnmMtx, mModelData->getJointCount(), nullptr, pMtx != nullptr ? pMtx : mBaseTransformMtx);
    mDrawPose = pPose;
    if ((view & 1) == 0) {
        mNormalPose = pPose;
    } else {
        pPose->mNormalFrom = mNormalPose;
    }
}

void PlayerModel::viewCalcRef(u32 view, Model* pModel) {
    NebulaMtxCopy(ModelSys::mViewMtx, mViewAtCalc);
    ModelPose* pPose = mExtraPoses[view + _DC];
    capturePose(pPose, pModel->getAnmMtxBuffer(), pModel->getModelData()->getJointCount(), nullptr, mBaseTransformMtx);
    pPose->mCalcMode = 2;
    pPose->mNormalFrom = mNormalPose;
    mDrawPose = pPose;
}

void PlayerModel::viewCalcRefPos(u32 view, Model* pModel, const Vec3f& rPos, const Vec3f& rNormal) {
    viewCalcRef(view, pModel);
    ModelPose* pPose = mDrawPose;
    const u32 count = mModelData->getFullWeightCount();
    for (u32 i = 0; i < count; i++) {
        Mtx jointMtx;
        Pos3f orientation;
        Mtx flatten;
        Vec3f trans;
        Vec3f planar;
        const u32 joint = mModelData->getDrawMtxJoint(i);

        MR::extractMtxTrans(pModel->getAnmMtx(joint), &trans);
        MR::vecKillElement(trans - rPos, rNormal, &planar);
        trans = rPos + planar;

        NebulaMtxCopy(pModel->getAnmMtx(joint), jointMtx);
        MR::setMtxTrans(jointMtx, 0.0f, 0.0f, 0.0f);
        MR::makeMtxUpNoSupportPos(&orientation, rNormal, Vec3f(0.0f, 0.0f, 0.0f));
        NebulaMtxConcat(orientation.toMtxPtr(), jointMtx, jointMtx);
        NebulaMtxScale(flatten, 1.0f, 0.1f, 1.0f);
        NebulaMtxConcat(flatten, jointMtx, jointMtx);
        NebulaMtxInverse(orientation.toMtxPtr(), orientation.toMtxPtr());
        NebulaMtxConcat(orientation.toMtxPtr(), jointMtx, jointMtx);
        MR::setMtxTrans(jointMtx, trans.x, trans.y, trans.z);
        NebulaMtxCopy(jointMtx, pPose->mJoints[joint]);
    }
}

void PlayerModel::directDraw() {
    for (u32 i = 0; i < mModelData->getMaterialCount(); i++) {
        drawIn(mModelData->getMaterialAt(i), i);
    }
}

namespace {
    // The flat passes' colours by list: `a` marks a flat pass (2: register 0's
    // colour as the draw's lists left it), `alpha` its alpha (list 1 is the shadow texture's: list 14 with it, the soft
    // edge around it, at a quarter); list 5 writes depth only.
    struct OverrideColor {
        u8 r, g, b, alpha, a;
    };
    const OverrideColor kOverrideColors[16] = {
        {0, 0, 0, 0, 0},     {0, 0, 0, 255, 1},  {0, 0, 0, 0, 0},  {0, 0, 0, 64, 1},
        {255, 255, 0, 255, 1}, {0, 0, 0, 0, 1},  {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0},
        {0, 0, 0, 252, 1},   {0, 0, 0, 4, 1},    {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 2},     {35, 25, 25, 5, 1}, {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0},
    };
}  // namespace

void PlayerModel::drawIn(Material* pMaterial, u32 index) {
    MaterialDraw draw;
    if (!simpleDrawSetup(pMaterial, &draw)) {
        return;
    }

    s32 override = -1;
    for (u32 i = 0; i <= 15; i++) {
        if ((mFlags.word() & (1u << i)) != 0) {
            if (i == 6) {
                draw.overrideCoordinate(RenderTexture::idOf(MR::getScreenResTIMG()->image()), MR::getScreenWidth(), NebulaFrameClock::cScreenLines);
                continue;
            }
            sendFlagState(i, draw);
            if (i == 2) {
                draw.override(1, 0.0f, 0.0f, -1.0f);
            }
            if (i == 11) {
                draw.override(5, 0.0f, 0.0f, 32.0f);
            }
            if (kOverrideColors[i].a != 0) {
                override = static_cast< s32 >(i);
            }
        }
    }

    if (_1B8 != nullptr) {
        _1B4 = _1B8;
        _1B8 = nullptr;
    }
    if (_1B4 != nullptr) {
        _1B4->send(draw);
    }

    if (_1C8[index] != nullptr) {
        _1C4[index] = _1C8[index];
        _1C8[index] = nullptr;
    }
    if (_1C4[index] != nullptr) {
        draw.swapImage(0, RenderTexture::idOf(_1C4[index]));
    }
    if (override >= 0) {
        const OverrideColor& rColor = kOverrideColors[override];
        if (rColor.a == 2) {
            const f32* pRegister = draw.getRegister(MaterialRegister_0);
            draw.setFlatColor(pRegister[0], pRegister[1], pRegister[2], pRegister[3]);
        } else {
            const u8 alpha = override == 1 && (mFlags.word() & (1u << 14)) != 0 ? 64 : rColor.alpha;
            draw.setFlatColor(rColor.r / 255.0f, rColor.g / 255.0f, rColor.b / 255.0f, alpha / 255.0f);
        }
    }
    simpleDrawShape(pMaterial, draw);
}

bool PlayerModel::simpleDrawSetup(Material* pMaterial, MaterialDraw* pDraw) {
    if (pMaterial == nullptr || pMaterial->getMesh() == nullptr || pMaterial->getMesh()->isHidden()) {
        return false;
    }
    MaterialEntry* pEntry = getMaterialEntry(pMaterial->getIndex());
    MeshEntry* pShape = getMeshEntry(pMaterial->getMesh()->getIndex());
    pDraw->send(pEntry->getMaterial());
    pDraw->sendOwn(pShape->mMaterial, pEntry->getMaterial());
    return true;
}

void PlayerModel::simpleDrawShape(Material* pMaterial, MaterialDraw& rDraw) {
    Mesh* pMesh = pMaterial->getMesh();
    if (!getMeshEntry(pMesh->getIndex())->checkFlag(MeshEntry::Flag_Hidden)) {
        if (mDrawHook != nullptr) {
            mDrawHook(rDraw);
        }
        rDraw.draw(this, pMesh);
    }
}

void PlayerModel::sendFlagState(u32 flag, MaterialDraw& rDraw) {
    PassState state = PassStates::fromWord(rDraw.getState());
    switch (flag) {
    case 0:
        state.cull = NEBULA_CULL_FRONT;
        break;
    // Flags 1, 3, 4, 5, 8, 9 and 13 replace the material's program with a flat colour (kOverrideColors, drawn by drawIn
    // as a native material): their fixed state here.
    case 1:
        PassStates::setDepth(state, false, NEBULA_COMPARE_ALWAYS, false);
        state.cull = NEBULA_CULL_BACK;
        PassStates::setNoBlend(state);
        state.color_write = 0;
        state.alpha_write = 1;
        break;
    // Texture 0 at alpha 1/4 added (MaterialDraw::override mode 1).
    case 2:
        state.cull = NEBULA_CULL_FRONT;
        PassStates::setDepth(state, true, NEBULA_COMPARE_LEQUAL, true);
        PassStates::setBlend(state, NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_ONE);
        state.color_write = 1;
        state.alpha_write = 0;
        break;
    // Where the model is behind a surface: marked, and the fill darkens it.
    case 3:
        PassStates::setDepth(state, true, NEBULA_COMPARE_GEQUAL, false);
        state.cull = NEBULA_CULL_BACK;
        PassStates::setNoBlend(state);
        state.color_write = 0;
        state.alpha_write = 0;
        state.stencil_func = NEBULA_STENCIL_ALWAYS;
        state.stencil_op = NEBULA_STENCIL_ADD;
        break;
    case 4:
        PassStates::setDepth(state, true, NEBULA_COMPARE_LEQUAL, false);
        state.cull = NEBULA_CULL_FRONT;
        PassStates::setBlend(state, NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_INV_SRC_ALPHA);
        state.color_write = 1;
        state.alpha_write = 0;
        break;
    case 5:
        PassStates::setDepth(state, true, NEBULA_COMPARE_ALWAYS, true);
        state.cull = NEBULA_CULL_FRONT;
        PassStates::setNoBlend(state);
        state.color_write = 0;
        state.alpha_write = 0;
        break;
    // The reflection model: its program (one stage: texture 0 halved, stage 0's alpha above 0x20, no channels) is
    // MaterialDraw::override mode 5.
    case 11:
        PassStates::setDepth(state, true, NEBULA_COMPARE_LEQUAL, true);
        state.cull = NEBULA_CULL_FRONT;
        PassStates::setNoBlend(state);
        state.color_write = 1;
        state.alpha_write = 1;
        break;
    // The blur ghosts in colour 0 (the ghost's tint sets it).
    case 12:
        PassStates::setDepth(state, true, NEBULA_COMPARE_LEQUAL, false);
        state.cull = NEBULA_CULL_BACK;
        PassStates::setBlend(state, NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_ONE);
        state.color_write = 1;
        state.alpha_write = 0;
        break;
    case 13:
        PassStates::setDepth(state, true, NEBULA_COMPARE_GEQUAL, false);
        state.cull = NEBULA_CULL_BACK;
        PassStates::setBlend(state, NEBULA_FACTOR_ONE, NEBULA_FACTOR_SRC_ALPHA);
        state.color_write = 1;
        state.alpha_write = 0;
        break;
    // Only where the dark mask did not mark.
    case 15:
        PassStates::setNoBlend(state);
        state.color_write = 1;
        state.alpha_write = 0;
        state.stencil_func = NEBULA_STENCIL_IF_ZERO;
        state.stencil_op = NEBULA_STENCIL_KEEP;
        break;
    default:
        break;
    }
    rDraw.setState(state.word());
}

void PlayerModel::initExtraPoses(u8 count) {
    _DD = count;
    for (u32 i = 0; i < count; i++) {
        mExtraPoses[i] = new ModelPose;
        initPose(mExtraPoses[i]);
    }
}

void PlayerModel::copyExtraPoses(const PlayerModel* pModel) {
    _DD = pModel->_DD;
    for (u32 i = 0; i < _DD; i++) {
        mExtraPoses[i] = pModel->mExtraPoses[i];
    }
}

void PlayerModel::swapDrawBuffer(u32 drawBuffer) {
    if (_DC == drawBuffer) {
        _DC = 0;
        return;
    }
    _DC = drawBuffer;
}

void PlayerModel::setDynamicList(const MarioModelTint* pTint) {
    _1B8 = pTint;
}

void MarioModelTint::send(MaterialDraw& rDraw) const {
    switch (kind) {
    case Kind_Alpha:
        rDraw.setRegister(MaterialRegister_2, color0);
        {
            PassState state = PassStates::fromWord(rDraw.getState());
            PassStates::setBlend(state, NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_INV_SRC_ALPHA);
            state.cull = NEBULA_CULL_BACK;
            rDraw.setState(state.word());
        }
        rDraw.applyPass(NEBULA_FORM_PASS_PLAYER_OPACITY);
        break;
    case Kind_ConstantAlpha:
        rDraw.overrideAlpha(alpha / 255.0f);
        break;
    case Kind_ReflectAlpha:
        rDraw.setRegister(MaterialRegister_2, color0);
        rDraw.applyPass(NEBULA_FORM_PASS_REFLECTION_ALPHA);
        break;
    case Kind_Light:
        rDraw.setAmbient(0, color0);
        rDraw.setMaterialColor(0, color1);
        if (hasFog) {
            rDraw.setFog(fogStart, fogEnd, fogNear, fogFar, fogColor);
        }
        rDraw.applyPass(NEBULA_FORM_PASS_PLAYER_LIGHT);
        break;
    case Kind_Ghost:
        rDraw.setRegister(MaterialRegister_0, color0);
        break;
    default:
        break;
    }
}

PlayerModel::PlayerModel(ModelData* pData, u32 flags, u32 unused) : Model(pData, flags, unused) {
    _DC = 0;
    _DD = 0;
    _1E4 = 0;
    _1E5 = 0;
    mFlags.clear();
    _1B4 = nullptr;
    _1B8 = nullptr;
    mDrawHook = nullptr;
    memset(mExtraPoses, 0, sizeof(mExtraPoses));

    const u32 count = mModelData->getMaterialCount();
    _1C4 = new const void*[count];
    _1C8 = new const void*[count];
    for (u32 i = 0; i < count; i++) {
        _1C4[i] = nullptr;
        _1C8[i] = nullptr;
    }
}

PlayerModel::~PlayerModel() {
}
