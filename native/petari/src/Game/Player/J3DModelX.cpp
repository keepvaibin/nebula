#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <nebula_cooked_material.h>
#include "Game/Player/J3DModelX.hpp"
#include "Game/Util/NativeShader.hpp"
#include "JSystem/J3DGraphBase/J3DNativeMaterial.hpp"
#include "Game/System/ShapePacketUserData.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include <JSystem/J3DGraphAnimator/J3DMtxBuffer.hpp>
#include <JSystem/J3DGraphBase/J3DMaterial.hpp>
#include <JSystem/J3DGraphBase/J3DShapeDraw.hpp>
#include <JSystem/J3DGraphBase/J3DShapeMtx.hpp>
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaFrameClock.hpp"

class J3DMtxBuffer2 : public J3DMtxBuffer {
public:
    void rotationMtx(MtxPtr mtx) {
        mpDrawMtxArr[1][mCurrentViewNo] = reinterpret_cast< Mtx* >(mtx);
    }
    void calcNrmMtx2();
    void calcDrawMtx2(u32, const Vec&, const Mtx&, J3DMtxBuffer*);
    void calcDrawMtx3(u32, const Vec&, const Mtx&, J3DMtxBuffer*, const TVec3f&, const TVec3f&);
};

class J3DShapeX : public J3DShape {};

class J3DShapePacketX : public J3DShapePacket {};

void J3DModelX::viewCalc2() {
    J3DMtxBuffer2* buffer = static_cast< J3DMtxBuffer2* >(mMtxBuffer);
    buffer->swapDrawMtx();
    buffer->swapNrmMtx();
    buffer->calcDrawMtx(getMtxCalcMode(), mBaseScale, mBaseTransformMtx);
    buffer->calcNrmMtx2();

    calcBBoardMtx();
    calcBumpMtx();
    prepareShapePackets();
}

void J3DModelX::setDrawView(u32 view) {
    static_cast< J3DMtxBuffer2* >(mMtxBuffer)->rotationMtx(*mExtraMtxBuffer[view + _DC]);
}

void J3DModelX::setDrawViewBuffer(MtxPtr mtx) {
    mMtxBuffer->mpDrawMtxArr[1][mMtxBuffer->mCurrentViewNo] = reinterpret_cast< Mtx* >(mtx);
}

void J3DModelX::copyAnmMtxBuffer(const J3DModelX* model) {
    mMtxBuffer->mpAnmMtx = model->mMtxBuffer->mpAnmMtx;
}

void J3DModelX::viewCalc3(u32 view, MtxPtr mtx) {
    J3DMtxBuffer2* buffer = static_cast< J3DMtxBuffer2* >(mMtxBuffer);
    buffer->rotationMtx(*mExtraMtxBuffer[view + _DC]);

    if (mtx) {
        buffer->calcDrawMtx(getMtxCalcMode(), mBaseScale, reinterpret_cast< const Mtx& >(mtx));
    } else {
        buffer->calcDrawMtx(getMtxCalcMode(), mBaseScale, mBaseTransformMtx);
    }

    if (!(view & 1)) {
        buffer->swapNrmMtx();
        buffer->calcNrmMtx2();
    }

    calcBBoardMtx();
    calcBumpMtx();
    prepareShapePackets();
}

void J3DModelX::viewCalcRef(u32 view, J3DModel* model) {
    J3DMtxBuffer2* buffer = static_cast< J3DMtxBuffer2* >(mMtxBuffer);
    buffer->rotationMtx(*mExtraMtxBuffer[view + _DC]);
    buffer->calcDrawMtx2(getMtxCalcMode(), mBaseScale, mBaseTransformMtx, model->getMtxBuffer());
    prepareShapePackets();
}

void J3DModelX::viewCalcRefPos(u32 view, J3DModel* model, const TVec3f& pos, const TVec3f& normal) {
    J3DMtxBuffer2* buffer = static_cast< J3DMtxBuffer2* >(mMtxBuffer);
    buffer->rotationMtx(*mExtraMtxBuffer[view + _DC]);
    buffer->calcDrawMtx3(getMtxCalcMode(), mBaseScale, mBaseTransformMtx, model->getMtxBuffer(), pos, normal);
    prepareShapePackets();
}

void J3DMtxBuffer2::calcNrmMtx2() {
    u16 count = mJointTree->getDrawMtxNum();

    for (u16 i = 0; i < count; i++) {
        J3DPSCalcInverseTranspose(*getDrawMtx(i), *getNrmMtx(i));
    }

}

void J3DMtxBuffer2::calcDrawMtx2(u32 mode, const Vec& scale, const Mtx& base, J3DMtxBuffer* buffer) {
    Mtx viewBase;
    J3DCalcViewBaseMtx(j3dSys.getViewMtx(), scale, base, viewBase);

    for (u16 i = 0, count = mJointTree->getDrawFullWgtMtxNum(); i < count; i++) {
        NebulaMtxConcat(viewBase, buffer->getAnmMtx(mJointTree->getDrawMtxIndex(i)), *getDrawMtx(i));
    }

    if (mJointTree->getDrawMtxNum() > mJointTree->getDrawFullWgtMtxNum()) {
        J3DPSMtxArrayConcat(viewBase, buffer->getWeightAnmMtx(0), *getDrawMtx(mJointTree->getDrawFullWgtMtxNum()), mJointTree->getWEvlpMtxNum());
    }
}

void J3DMtxBuffer2::calcDrawMtx3(u32 mode, const Vec& scale, const Mtx& base, J3DMtxBuffer* buffer, const TVec3f& pos, const TVec3f& normal) {
    Mtx viewBase;
    J3DCalcViewBaseMtx(j3dSys.getViewMtx(), scale, base, viewBase);
    u16 count = mJointTree->getDrawFullWgtMtxNum();

    for (u16 i = 0; i < count; i++) {
        Mtx jointMtx;
        TPos3f orientation;
        Mtx flatten;
        TVec3f trans;
        TVec3f planar;

        MR::extractMtxTrans(buffer->getAnmMtx(mJointTree->getDrawMtxIndex(i)), &trans);
        MR::vecKillElement(trans - pos, normal, &planar);
        trans = pos + planar;

        NebulaMtxCopy(buffer->getAnmMtx(mJointTree->getDrawMtxIndex(i)), jointMtx);
        MR::setMtxTrans(jointMtx, 0.0f, 0.0f, 0.0f);
        MR::makeMtxUpNoSupportPos(&orientation, normal, TVec3f(0.0f, 0.0f, 0.0f));
        NebulaMtxConcat(orientation.toMtxPtr(), jointMtx, jointMtx);
        NebulaMtxScale(flatten, 1.0f, 0.1f, 1.0f);
        NebulaMtxConcat(flatten, jointMtx, jointMtx);
        NebulaMtxInverse(orientation.toMtxPtr(), orientation.toMtxPtr());
        NebulaMtxConcat(orientation.toMtxPtr(), jointMtx, jointMtx);
        MR::setMtxTrans(jointMtx, trans.x, trans.y, trans.z);
        NebulaMtxConcat(viewBase, jointMtx, *getDrawMtx(i));
    }
}

void J3DModelX::directDraw(J3DModel* model) {
    j3dSys.setModel(this);
    mModelData->syncJ3DSysFlags();
    j3dSys.setTexture(mModelData->getTexture());

    bool fog = false;
    if (_1D0) {
        fog = true;
        _1D0--;
    }
    for (u16 i = 0; i < mModelData->getMaterialNum(); i++) {
        J3DMaterial* material = mModelData->getMaterialNodePointer(i);
        _1C0 = i;
        if (model) {
            drawIn(material, fog, model->getBaseTRMtx(), model);
        } else {
            drawIn(material, fog, mBaseTransformMtx, nullptr);
        }
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

void J3DModelX::drawIn(J3DMaterial* material, bool fog, MtxPtr base, J3DModel* model) {
    if (!material) {
        return;
    }
    if (!material->getShape()) {
        return;
    }
    if (material->getShape()->checkFlag(1)) {
        return;
    }
    J3DMatPacket* matPacket = getMatPacket(material->getIndex());
    J3DShapePacket* packet = model ? model->getShapePacket(material->getShape()->getIndex()) : getShapePacket(material->getShape()->getIndex());
    j3dSys.setMatPacket(matPacket);
    J3DNativeMaterial::send(matPacket->getMaterial());
    packet->getShape()->loadPreDrawSetting();
    J3DNativeMaterial::sendOwn(packet->mpMaterial, matPacket->getMaterial());

    if (fog) {
        TVec3f pos;
        MR::extractMtxTrans(mBaseTransformMtx, &pos);
        TDDraw::mixFogColor(pos, _1D4, _1D8);
    }

    if (MR::getJ3DShapePacketUserData(packet)) {
        MR::getJ3DShapePacketUserData(packet)->load();
    }

    s32 override = -1;
    for (u32 i = 0; i <= 15; i++) {
        if (mFlags.word() & (1 << i)) {
            if (i == 6) {
                J3DNativeMaterial::overrideCoordinate(MR::getScreenResTIMG()->image(), MR::getScreenWidth(),
                                                      NebulaFrameClock::cScreenLines);
                continue;
            }
            sendFlagState(i);
            if (i == 2) {
                J3DNativeMaterial::override(1, 0.0f, 0.0f, -1.0f);
            }
            if (i == 11) {
                J3DNativeMaterial::override(5, 0.0f, 0.0f, 32.0f);
            }
            if (kOverrideColors[i].a != 0) {
                override = static_cast< s32 >(i);
            }
        }
    }

    if (_1B8) {
        _1B4 = _1B8;
        _1B8 = nullptr;
    }
    if (_1B4) {
        _1B4->send();
    }

    u32 index = _1C0;
    if (_1C8[index]) {
        _1C4[index] = _1C8[index];
        _1C8[index] = nullptr;
    }
    if (_1C4[index]) {
        J3DNativeMaterial::swapImage(0, _1C4[index]);
    }
    if (override >= 0) {
        // runtime/src/render/materials/flat_color.hlsl: the pass's colour over the material's program (no textures), with the blending, depth and culling the lists set.
        NebulaNativeMaterial material;
        NativeShader::init(material, NEBULA_SHADER_FLAT_COLOR, NativeShader::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS,
                           false, NEBULA_CULL_NONE);
        material.blend_op = NEBULA_NATIVE_KEPT;
        material.depth_test = NEBULA_NATIVE_KEPT;
        material.cull = NEBULA_NATIVE_KEPT;
        const OverrideColor& rColor = kOverrideColors[override];
        if (rColor.a == 2) {
            const f32* pRegister = J3DNativeMaterial::currentRegister(1);
            for (u32 c = 0; c < 4; c++) {
                material.constants[c] = pRegister[c];
            }
        } else {
            material.constants[0] = rColor.r / 255.0f;
            material.constants[1] = rColor.g / 255.0f;
            material.constants[2] = rColor.b / 255.0f;
            material.constants[3] = (override == 1 && (mFlags.word() & (1 << 14)) != 0 ? 64 : rColor.alpha) / 255.0f;
        }
        NativeShader::send(material);
    }
    if (_120) {
        _120(_128, _1C0);
    }

    J3DShape* original = nullptr;
    if (_12C) {
        J3DShapePacket* replacement = _12C->getShapePacket(material->getShape()->getIndex());
        original = packet->getShape();
        packet->setShape(replacement->getShape());
        packet->mpShape->mDrawMtx = original->mDrawMtx;
        packet->mpShape->mDrawMtxData = original->mDrawMtxData;
        packet->mpShape->mCurrentMtx = material->mCurrentMtx;
    }

    shapePacketDrawFast(static_cast< J3DShapePacketX* >(packet));
    if (_12C) {
        packet->mpShape = original;
    }
}

bool J3DModelX::simpleDrawSetup(J3DMaterial* material) {
    if (!material) {
        return false;
    }
    if (!material->getShape()) {
        return false;
    }
    if (material->getShape()->checkFlag(1)) {
        return false;
    }

    J3DMatPacket* matPacket = getMatPacket(material->getIndex());
    J3DShapePacket* packet = getShapePacket(material->getShape()->getIndex());
    j3dSys.setMatPacket(matPacket);
    J3DNativeMaterial::send(matPacket->getMaterial());
    packet->getShape()->loadPreDrawSetting();
    J3DNativeMaterial::sendOwn(packet->mpMaterial, matPacket->getMaterial());
    return true;
}

void J3DModelX::simpleDrawShape(J3DMaterial* material) {
    getShapePacket(material->getShape()->getIndex())->drawFast();
}

void J3DModelX::sendFlagState(u32 flag) {
    using namespace J3DPass;
    switch (flag) {
    case 0:
        cull(NEBULA_CULL_FRONT);
        break;
    // Flags 1, 3, 4, 5, 8, 9 and 13 replace the material's program with a flat colour (kOverrideColors, drawn by drawIn
    // as a native material): their fixed state here.
    case 1:
        depth(false, NEBULA_COMPARE_ALWAYS, false);
        cull(NEBULA_CULL_BACK);
        noBlend();
        writeColor(false);
        writeAlpha(true);
        break;
    // Texture 0 at alpha 1/4 added (J3DNativeMaterial::override mode 1).
    case 2:
        cull(NEBULA_CULL_FRONT);
        depth(true, NEBULA_COMPARE_LEQUAL, true);
        blend(NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_ONE);
        writeColor(true);
        writeAlpha(false);
        break;
    // Where the model is behind a surface: marked, and the fill darkens it.
    case 3:
        depth(true, NEBULA_COMPARE_GEQUAL, false);
        cull(NEBULA_CULL_BACK);
        noBlend();
        writeColor(false);
        writeAlpha(false);
        stencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ADD);
        break;
    case 4:
        depth(true, NEBULA_COMPARE_LEQUAL, false);
        cull(NEBULA_CULL_FRONT);
        blend(NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_INV_SRC_ALPHA);
        writeColor(true);
        writeAlpha(false);
        break;
    case 5:
        depth(true, NEBULA_COMPARE_ALWAYS, true);
        cull(NEBULA_CULL_FRONT);
        noBlend();
        writeColor(false);
        writeAlpha(false);
        break;
    // The reflection model: its program (one stage: texture 0 halved, stage 0's alpha above 0x20, no channels) is
    // J3DNativeMaterial::override mode 5.
    case 11:
        depth(true, NEBULA_COMPARE_LEQUAL, true);
        cull(NEBULA_CULL_FRONT);
        noBlend();
        writeColor(true);
        writeAlpha(true);
        break;
    // The blur ghosts in colour 0 (the ghost's tint sets it).
    case 12:
        depth(true, NEBULA_COMPARE_LEQUAL, false);
        cull(NEBULA_CULL_BACK);
        blend(NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_ONE);
        writeColor(true);
        writeAlpha(false);
        break;
    case 13:
        depth(true, NEBULA_COMPARE_GEQUAL, false);
        cull(NEBULA_CULL_BACK);
        blend(NEBULA_FACTOR_ONE, NEBULA_FACTOR_SRC_ALPHA);
        writeColor(true);
        writeAlpha(false);
        break;
    // Only where the dark mask did not mark.
    case 15:
        noBlend();
        writeColor(true);
        writeAlpha(false);
        stencil(NEBULA_STENCIL_IF_ZERO, NEBULA_STENCIL_KEEP);
        break;
    default:
        break;
    }
}

void MarioModelTint::send() const {
    J3DNativeShape::flushState();
    switch (kind) {
    case Kind_Alpha:
        J3DNativeMaterial::setCurrentRegister(3, color0);
        J3DPass::blend(NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_INV_SRC_ALPHA);
        J3DPass::cull(NEBULA_CULL_BACK);
        J3DNativeMaterial::applyPass(NEBULA_PASS_PLAYER_OPACITY);
        break;
    case Kind_ConstantAlpha:
        J3DNativeMaterial::overrideAlpha(alpha / 255.0f);
        break;
    case Kind_ReflectAlpha:
        J3DNativeMaterial::setCurrentRegister(3, color0);
        J3DNativeMaterial::applyPass(NEBULA_PASS_REFLECTION_ALPHA);
        break;
    case Kind_Light:
        J3DNativeMaterial::setCurrentAmbient(0, color0);
        J3DNativeMaterial::setCurrentMaterialColor(0, color1);
        if (hasFog) {
            J3DNativeMaterial::setCurrentFog(fogStart, fogEnd, fogNear, fogFar, fogColor);
        }
        J3DNativeMaterial::applyPass(NEBULA_PASS_PLAYER_LIGHT);
        break;
    case Kind_Ghost:
        J3DNativeMaterial::setCurrentRegister(1, color0);
        break;
    default:
        break;
    }
}

J3DModelX::J3DModelX(J3DModelData* data, u32 flags, u32 bufferFlags) : J3DModel(data, flags, bufferFlags) {
    _DC = 0;
    _DD = 0;
    _120 = nullptr;
    _128 = nullptr;
    _12C = nullptr;
    mShapeCallback = nullptr;
    _1E4 = 0;
    _1E5 = 0;

    _1E0 = -1;
    _1DC = 0x32323232;
    _1D8 = 0xFF0000FF;
    mFlags.clear();
    _1D4 = 0.0f;
    _1D0 = 0;
    _1B4 = nullptr;
    _1B8 = nullptr;

    u32 count = mModelData->getMaterialNum();
    _1C4 = new const void*[count];
    _1C8 = new const void*[count];
    for (u32 i = 0; i < count; i++) {
        _1C4[i] = nullptr;
        _1C8[i] = nullptr;
    }
    _DC = 0;
}

void J3DModelX::shapePacketDrawFast(J3DShapePacketX* packet) const {
    if (!(packet->mFlags & 0x10) && packet->getShape()) {
        packet->prepareDraw();
        shapeDrawFast(static_cast< J3DShapeX* >(packet->getShape()));
    }
}

void J3DModelX::shapeDrawFast(J3DShapeX* shape) const {
    shape->prepareGeometry();
    if (J3DShape::sEnvelopeFlag && !shape->mHasPNMTXIdx) {
        shape->mCurrentMtx.load();
    }

    shape->setArrayAndBindPipeline();

    if (!(shape->mFlags & 0x200)) {
        u16 count = shape->mMtxGroupNum;
        for (u16 i = 0; i < count; i++) {
            if (shape->mShapeMtx[i]) {
                shape->mShapeMtx[i]->load();
            }
            if (mShapeCallback) {
                mShapeCallback(shape);
            }
            if (shape->mShapeDraw[i]) {
                shape->mShapeDraw[i]->draw();
            }
        }
    } else {
        J3DNativeShape::setPosition(0, *j3dSys.getShapePacket()->getBaseMtxPtr());
        J3DNativeShape::setNormal(0, *j3dSys.getShapePacket()->getBaseMtxPtr());
        u16 count = shape->mMtxGroupNum;
        for (u16 i = 0; i < count; i++) {
            if (shape->mShapeDraw[i]) {
                shape->mShapeDraw[i]->draw();
            }
        }
    }
}

J3DModelX::~J3DModelX() {
}
