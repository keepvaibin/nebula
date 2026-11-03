#define J3D_TEV_STAGE_MUTABLE_DEFAULTS

#include "JSystem/J3DGraphBase/J3DMaterial.hpp"
#include "JSystem/J3DGraphBase/J3DNativeMaterial.hpp"
#include "JSystem/J3DGraphBase/J3DSys.hpp"

J3DColorBlock* J3DMaterial::createColorBlock(u32 flags) {
    switch (flags) {
    case 0:
        return new J3DColorBlock(J3DColorBlock::Kind_LightOff);
    case 0x40000000:
        return new J3DColorBlock(J3DColorBlock::Kind_LightOn);
    case 1u << 31:
        return new J3DColorBlock(J3DColorBlock::Kind_AmbientOn);
    default:
        return NULL;
    }
}

J3DTexGenBlock* J3DMaterial::createTexGenBlock(u32 flags) {
    return flags == 0x8000000 ? new J3DTexGenBlock(4, true) : new J3DTexGenBlock(8, true);
}

J3DRegisterBlock* J3DMaterial::createRegisterBlock(int tevStageNum) {
    if (tevStageNum <= 1) {
        return new J3DRegisterBlock(1, true, false);
    }
    if (tevStageNum == 2) {
        return new J3DRegisterBlock(2, true, true);
    }
    if (tevStageNum <= 4) {
        return new J3DRegisterBlock(4, true, true);
    }
    return tevStageNum <= 16 ? new J3DRegisterBlock(8, true, true) : nullptr;
}

J3DIndBlock* J3DMaterial::createIndBlock(int flags) {
    return new J3DIndBlock(flags != 0);
}

J3DOutputBlock* J3DMaterial::createOutputBlock(u32 flags, u32 materialMode) {
    J3DOutputBlock* rv = NULL;
    if (flags == 0) {
        if (materialMode & 1) {
            rv = new J3DOutputBlock(J3DOutputBlock::Kind_Opaque);
            return rv;
        } else if (materialMode & 2) {
            rv = new J3DOutputBlock(J3DOutputBlock::Kind_TexEdge);
            return rv;
        } else if (materialMode & 4) {
            rv = new J3DOutputBlock(J3DOutputBlock::Kind_Translucent);
            return rv;
        }
    }

    if (flags == 0x10000000) {
        rv = new J3DOutputBlock(J3DOutputBlock::Kind_Full);
    } else if (flags == 0x20000000) {
        rv = new J3DOutputBlock(J3DOutputBlock::Kind_FogOff);
    }

    return rv;
}

void J3DMaterial::initialize() {
    mShape = NULL;
    mNext = NULL;
    mJoint = NULL;
    mMaterialMode = 1;
    mIndex = -1;
    mInvalid = 0;
    mDiffFlag = 0;
    mColorBlock = NULL;
    mTexGenBlock = NULL;
    mRegisterBlock = NULL;
    mIndBlock = NULL;
    mOutputBlock = NULL;
    mpOrigMaterial = NULL;
    mMaterialAnm = NULL;
    mSharedSnapshot = NULL;
    mNativeMaterial = NULL;
}


void J3DMaterial::makeSnapshot() {
    if (!j3dSys.getMatPacket()->isLocked()) {
        j3dSys.getMatPacket()->mDiffFlag = mDiffFlag;
        J3DNativeMaterial::capture(this, j3dSys.getMatPacket()->mpTexture, j3dSys.getMatPacket()->getSnapshot());
    }
}

void J3DMaterial::makeSharedSnapshot() {
    J3DNativeMaterial::capture(this, j3dSys.getTexture(), mSharedSnapshot);
}

void J3DMaterial::load() {
    j3dSys.setMaterialMode(mMaterialMode);
    if (!j3dSys.checkFlag(2)) {
        loadNBTScale(*mTexGenBlock->getNBTScale());
    }
}

void J3DMaterial::loadSharedMaterial() {
    j3dSys.setMaterialMode(mMaterialMode);
    if (!j3dSys.checkFlag(2)) {
        J3DNativeMaterial::send(this, j3dSys.getTexture(), mSharedSnapshot);
        loadNBTScale(*mTexGenBlock->getNBTScale());
    }
}

void J3DMaterial::patch() {
    j3dSys.getMatPacket()->mDiffFlag = mDiffFlag;
    J3DNativeMaterial::capture(this, j3dSys.getMatPacket()->mpTexture, j3dSys.getMatPacket()->getSnapshot());
}

void J3DMaterial::diff(u32 diffFlags) {
    // An instance's animated values, into its shape packet's differed snapshot (the parts `diffFlags` names).
    if (j3dSys.getMatPacket()->isEnabled_Diff()) {
        J3DNativeMaterial::capture(this, j3dSys.getMatPacket()->mpTexture,
                                   j3dSys.getMatPacket()->mpInitShapePacket->getSnapshot(), diffFlags);
    }
}

void J3DMaterial::calc(f32 const (*param_0)[4]) {
    if (j3dSys.checkFlag(0x40000000)) {
        mTexGenBlock->calcPostTexMtx(param_0);
    } else {
        mTexGenBlock->calc(param_0);
    }

    calcCurrentMtx();
    setCurrentMtx();
}

void J3DMaterial::calcDiffTexMtx(f32 const (*param_0)[4]) {
    if (j3dSys.checkFlag(0x40000000)) {
        mTexGenBlock->calcPostTexMtxWithoutViewMtx(param_0);
    } else {
        mTexGenBlock->calcWithoutViewMtx(param_0);
    }
}

void J3DMaterial::setCurrentMtx() {
    mShape->setCurrentMtx(mCurrentMtx);
}

void J3DMaterial::calcCurrentMtx() {
    if (!j3dSys.checkFlag(0x40000000)) {
        mCurrentMtx.setCurrentTexMtx(getTexCoord(0)->getTexGenMtx(), getTexCoord(1)->getTexGenMtx(), getTexCoord(2)->getTexGenMtx(),
                                     getTexCoord(3)->getTexGenMtx(), getTexCoord(4)->getTexGenMtx(), getTexCoord(5)->getTexGenMtx(),
                                     getTexCoord(6)->getTexGenMtx(), getTexCoord(7)->getTexGenMtx());
    } else {
        mCurrentMtx.setCurrentTexMtx(getTexCoord(0)->getTexMtxReg(), getTexCoord(1)->getTexMtxReg(), getTexCoord(2)->getTexMtxReg(),
                                     getTexCoord(3)->getTexMtxReg(), getTexCoord(4)->getTexMtxReg(), getTexCoord(5)->getTexMtxReg(),
                                     getTexCoord(6)->getTexMtxReg(), getTexCoord(7)->getTexMtxReg());
    }
}

void J3DMaterial::copy(J3DMaterial* pOther) {
    mNativeMaterial = pOther->mNativeMaterial;
    mColorBlock->reset(pOther->mColorBlock);
    mTexGenBlock->reset(pOther->mTexGenBlock);
    mRegisterBlock->reset(pOther->mRegisterBlock);
    mIndBlock->reset(pOther->mIndBlock);
    mOutputBlock->reset(pOther->mOutputBlock);
}

void J3DMaterial::reset() {
    if ((~mDiffFlag & J3DDiffFlag_Changed) == 0) {
        mDiffFlag &= ~J3DDiffFlag_Changed;
        mMaterialMode = mpOrigMaterial->mMaterialMode;
        mInvalid = mpOrigMaterial->mInvalid;
        mMaterialAnm = NULL;
        copy(mpOrigMaterial);
    }
}

void J3DMaterial::change() {
    if ((mDiffFlag & (J3DDiffFlag_Changed | J3DDiffFlag_Unk40000000)) == 0) {
        mDiffFlag |= J3DDiffFlag_Changed;
        mMaterialAnm = NULL;
    }
}

s32 J3DMaterial::newSharedSnapshot() {
    if (mSharedSnapshot == NULL) {
        mSharedSnapshot = new J3DMaterialSnapshot();
        if (mSharedSnapshot == NULL) {
            return kJ3DError_Alloc;
        }

        s32 ret = mSharedSnapshot->newSnapshot();
        if (ret != kJ3DError_Success) {
            return ret;
        }
    }

    return kJ3DError_Success;
}


