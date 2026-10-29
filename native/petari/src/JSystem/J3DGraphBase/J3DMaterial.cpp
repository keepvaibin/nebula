#define J3D_TEV_STAGE_MUTABLE_DEFAULTS

#include "JSystem/J3DGraphBase/J3DMaterial.hpp"
#include "JSystem/J3DGraphBase/J3DNativeMaterial.hpp"
#include "JSystem/J3DGraphBase/J3DSys.hpp"

J3DColorBlock* J3DMaterial::createColorBlock(u32 flags) {
    J3DColorBlock* rv = NULL;
    switch (flags) {
    case 0:
        rv = new J3DColorBlockLightOff();
        break;
    case 0x40000000:
        rv = new J3DColorBlockLightOn();
        break;
    case 0x80000000:
        rv = new J3DColorBlockAmbientOn();
        break;
    }

    return rv;
}

J3DTexGenBlock* J3DMaterial::createTexGenBlock(u32 flags) {
    J3DTexGenBlock* rv = NULL;
    switch (flags) {
    case 0x8000000:
        rv = new J3DTexGenBlock4();
        break;
    case 0:
    default:
        rv = new J3DTexGenBlockBasic();
    }

    return rv;
}

J3DTevBlock* J3DMaterial::createTevBlock(int tevStageNum) {
    J3DTevBlock* rv = NULL;
    if (tevStageNum <= 1) {
        rv = new J3DTevBlock1();
    } else if (tevStageNum == 2) {
        rv = new J3DTevBlock2();
    } else if (tevStageNum <= 4) {
        rv = new J3DTevBlock4();
    } else if (tevStageNum <= 16) {
        rv = new J3DTevBlock16();
    }

    return rv;
}

J3DIndBlock* J3DMaterial::createIndBlock(int flags) {
    J3DIndBlock* rv = NULL;
    if (flags != 0) {
        rv = new J3DIndBlockFull();
    } else {
        rv = new J3DIndBlockNull();
    }

    return rv;
}

J3DPEBlock* J3DMaterial::createPEBlock(u32 flags, u32 materialMode) {
    J3DPEBlock* rv = NULL;
    if (flags == 0) {
        if (materialMode & 1) {
            rv = new J3DPEBlockOpa();
            return rv;
        } else if (materialMode & 2) {
            rv = new J3DPEBlockTexEdge();
            return rv;
        } else if (materialMode & 4) {
            rv = new J3DPEBlockXlu();
            return rv;
        }
    }

    if (flags == 0x10000000) {
        rv = new J3DPEBlockFull();
    } else if (flags == 0x20000000) {
        rv = new J3DPEBlockFogOff();
    }

    return rv;
}

u32 J3DMaterial::calcSizeColorBlock(u32 flags) {
    u32 rv = 0;
    switch (flags) {
    case 0:
        rv += sizeof(J3DColorBlockLightOff);
        break;
    case 0x40000000:
        rv += sizeof(J3DColorBlockLightOn);
        break;
    case 0x80000000:
        rv += sizeof(J3DColorBlockAmbientOn);
        break;
    }

    return rv;
}

u32 J3DMaterial::calcSizeTexGenBlock(u32 flags) {
    u32 rv = 0;
    switch (flags) {
    case 0x8000000:
        rv += sizeof(J3DTexGenBlock4);
        break;
    case 0:
    default:
        rv += sizeof(J3DTexGenBlockBasic);
    }

    return rv;
}

u32 J3DMaterial::calcSizeTevBlock(int tevStageNum) {
    u32 rv = 0;
    if (tevStageNum <= 1) {
        rv += sizeof(J3DTevBlock1);
    } else if (tevStageNum == 2) {
        rv += sizeof(J3DTevBlock2);
    } else if (tevStageNum <= 4) {
        rv += sizeof(J3DTevBlock4);
    } else if (tevStageNum <= 16) {
        rv += sizeof(J3DTevBlock16);
    }

    return rv;
}

u32 J3DMaterial::calcSizeIndBlock(int flags) {
    u32 rv = 0;
    if (flags != 0) {
        rv += sizeof(J3DIndBlockFull);
    } else {
        rv += sizeof(J3DIndBlockNull);
    }

    return rv;
}

u32 J3DMaterial::calcSizePEBlock(u32 flags, u32 materialMode) {
    u32 rv = 0;
    if (flags == 0) {
        if (materialMode & 1) {
            rv += sizeof(J3DPEBlockOpa);
        } else if (materialMode & 2) {
            rv += sizeof(J3DPEBlockTexEdge);
        } else if (materialMode & 4) {
            rv += sizeof(J3DPEBlockXlu);
        }
    } else if (flags == 0x10000000) {
        rv += sizeof(J3DPEBlockFull);
    } else if (flags == 0x20000000) {
        rv += sizeof(J3DPEBlockFogOff);
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
    mTevBlock = NULL;
    mIndBlock = NULL;
    mPEBlock = NULL;
    mpOrigMaterial = NULL;
    mMaterialAnm = NULL;
    mSharedDLObj = NULL;
}


void J3DMaterial::makeDisplayList() {
    if (!j3dSys.getMatPacket()->isLocked()) {
        j3dSys.getMatPacket()->mDiffFlag = mDiffFlag;
        J3DNativeMaterial::capture(this, j3dSys.getMatPacket()->mpTexture, j3dSys.getMatPacket()->getDisplayListObj());
    }
}

void J3DMaterial::makeSharedDisplayList() {
    J3DNativeMaterial::capture(this, j3dSys.getTexture(), mSharedDLObj);
}

void J3DMaterial::load() {
    j3dSys.setMaterialMode(mMaterialMode);
    if (!j3dSys.checkFlag(2)) {
        loadNBTScale(*mTexGenBlock->getNBTScale());
    }
}

void J3DMaterial::loadSharedDL() {
    j3dSys.setMaterialMode(mMaterialMode);
    if (!j3dSys.checkFlag(2)) {
        J3DNativeMaterial::send(this, j3dSys.getTexture(), mSharedDLObj);
        loadNBTScale(*mTexGenBlock->getNBTScale());
    }
}

void J3DMaterial::patch() {
    j3dSys.getMatPacket()->mDiffFlag = mDiffFlag;
    J3DNativeMaterial::capture(this, j3dSys.getMatPacket()->mpTexture, j3dSys.getMatPacket()->getDisplayListObj());
}

void J3DMaterial::diff(u32 diffFlags) {
    // An instance's animated values, into its shape packet's differed snapshot (the parts `diffFlags` names).
    if (j3dSys.getMatPacket()->isEnabled_Diff()) {
        J3DNativeMaterial::capture(this, j3dSys.getMatPacket()->mpTexture,
                                   j3dSys.getMatPacket()->mpInitShapePacket->getDisplayListObj(), diffFlags);
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
    mColorBlock->reset(pOther->mColorBlock);
    mTexGenBlock->reset(pOther->mTexGenBlock);
    mTevBlock->reset(pOther->mTevBlock);
    mIndBlock->reset(pOther->mIndBlock);
    mPEBlock->reset(pOther->mPEBlock);
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

s32 J3DMaterial::newSharedDisplayList() {
    if (mSharedDLObj == NULL) {
        mSharedDLObj = new J3DDisplayListObj();
        if (mSharedDLObj == NULL) {
            return kJ3DError_Alloc;
        }

        s32 ret = mSharedDLObj->newDisplayList();
        if (ret != kJ3DError_Success) {
            return ret;
        }
    }

    return kJ3DError_Success;
}


void J3DPatchedMaterial::initialize() {
    J3DMaterial::initialize();
}

void J3DPatchedMaterial::makeDisplayList() {
}

void J3DPatchedMaterial::makeSharedDisplayList() {
}

void J3DPatchedMaterial::load() {
    j3dSys.setMaterialMode(mMaterialMode);
    if (j3dSys.checkFlag(2)) {
        return;
    }
}

void J3DPatchedMaterial::loadSharedDL() {
    j3dSys.setMaterialMode(mMaterialMode);
    if (!j3dSys.checkFlag(0x02)) {
        J3DNativeMaterial::send(this, j3dSys.getTexture(), mSharedDLObj);
    }
}

void J3DPatchedMaterial::reset() {
}

void J3DPatchedMaterial::change() {
}

void J3DLockedMaterial::initialize() {
    J3DMaterial::initialize();
}

void J3DLockedMaterial::makeDisplayList() {
}

void J3DLockedMaterial::makeSharedDisplayList() {
}

void J3DLockedMaterial::load() {
    j3dSys.setMaterialMode(mMaterialMode);
    if (j3dSys.checkFlag(2)) {
        return;
    }
}

void J3DLockedMaterial::loadSharedDL() {
    j3dSys.setMaterialMode(mMaterialMode);
    if (!j3dSys.checkFlag(0x02)) {
        J3DNativeMaterial::send(this, j3dSys.getTexture(), mSharedDLObj);
    }
}

void J3DLockedMaterial::patch() {
}

void J3DLockedMaterial::diff(u32 diffFlags) {
}

void J3DLockedMaterial::calc(const Mtx param_0) {
}

void J3DLockedMaterial::reset() {
}

void J3DLockedMaterial::change() {
}
