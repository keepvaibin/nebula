#include "JSystem/J3DGraphBase/J3DPacket.hpp"
#include "JSystem/J3DGraphBase/J3DNativeMaterial.hpp"
#include "Game/Util/MotionSignal.hpp"
#include "JSystem/J3DGraphAnimator/J3DModel.hpp"
#include "JSystem/J3DGraphAnimator/J3DModelData.hpp"
#include "JSystem/J3DGraphAnimator/J3DMtxBuffer.hpp"
#include "JSystem/J3DGraphBase/J3DDrawBuffer.hpp"
#include "JSystem/J3DGraphBase/J3DMaterial.hpp"
#include "JSystem/J3DGraphBase/J3DShapeMtx.hpp"
#include "Game/System/NebulaArena.hpp"
#include <cstdio>
#include <cstring>

J3DError J3DMaterialSnapshot::newSnapshot() {
    mpNebulaMaterial = J3DNativeMaterial::create();
    return kJ3DError_Success;
}

int J3DPacket::entry(J3DDrawBuffer* pBuffer) {
    return 1;
}

void J3DPacket::addChildPacket(J3DPacket* pPacket) {
    if (mpFirstChild == NULL) {
        mpFirstChild = pPacket;
    } else {
        pPacket->setNextPacket(mpFirstChild);
        mpFirstChild = pPacket;
    }
}

J3DDrawPacket::J3DDrawPacket() {
    mFlags = 0;
    mpSnapshot = NULL;
    mpTexMtxObj = NULL;
}

J3DDrawPacket::~J3DDrawPacket() {
}

J3DError J3DDrawPacket::newSnapshot() {
    mpSnapshot = new J3DMaterialSnapshot();

    if (mpSnapshot == NULL)
        return kJ3DError_Alloc;

    return mpSnapshot->newSnapshot();
}

void J3DDrawPacket::draw() {
}

J3DMatPacket::J3DMatPacket() {
    mpShapePacket = NULL;
    mpInitShapePacket = NULL;
    mpMaterial = NULL;
    mDiffFlag = 0xFFFFFFFF;
    mpTexture = NULL;
    mpMaterialAnm = NULL;
}

J3DMatPacket::~J3DMatPacket() {
}

void J3DMatPacket::addShapePacket(J3DShapePacket* pShape) {
    if (mpShapePacket == NULL) {
        mpShapePacket = pShape;
    } else {
        pShape->setNextPacket(mpShapePacket);
        mpShapePacket = pShape;
    }
}

bool J3DMatPacket::isSame(J3DMatPacket* pOther) const {
    return mDiffFlag == pOther->mDiffFlag && (mDiffFlag >> 31) == 0;
}

void J3DMatPacket::draw() {
    mpMaterial->load();
    J3DNativeMaterial::send(mpMaterial, mpTexture, mpSnapshot);

    J3DShapePacket* packet = getShapePacket();
    packet->getShape()->loadPreDrawSetting();

    while (packet != NULL) {
        if (packet->getSnapshot() != NULL) {
            J3DNativeMaterial::sendDiffered(packet->getSnapshot());
        }

        packet->drawFast();
        packet = (J3DShapePacket*)packet->getNextPacket();
    }
}



J3DShapePacket::J3DShapePacket() {
    mpShape = NULL;
    mpMtxBuffer = NULL;
    mpBaseMtxPtr = NULL;
    mDiffFlag = 0;
    mpModel = NULL;
}

J3DShapePacket::~J3DShapePacket() {
}


J3DError J3DShapePacket::newDifferedSnapshot(u32 diffFlags) {
    mDiffFlag = diffFlags;

    J3DError ret = newSnapshot();
    if (ret != kJ3DError_Success) {
        return ret;
    }

    return kJ3DError_Success;
}

void J3DShapePacket::prepareDraw() const {
    MotionSignal::drawMaterial(this);
    MotionSignal::drawShapePacket(this);
    mpModel->getVertexBuffer()->setArray();
    j3dSys.setModel(mpModel);
    j3dSys.setShapePacket((J3DShapePacket*)this);

    mpShape->setCurrentViewNoPtr(mpMtxBuffer->getCurrentViewNoPtr());
    mpShape->setScaleFlagArray(mpMtxBuffer->getScaleFlagArray());
    mpShape->setDrawMtx(mpMtxBuffer->getDrawMtxPtrPtr());

    if (!mpShape->getNBTFlag()) {
        mpShape->setNrmMtx(mpMtxBuffer->getNrmMtxPtrPtr());
    } else {
        mpShape->setNrmMtx(mpMtxBuffer->mpBumpMtxArr[1][mpShape->getBumpMtxOffset()]);
    }

    mpModel->getModelData()->syncJ3DSysFlags();
}

void J3DShapePacket::draw() {
    if (!checkFlag(J3DShpFlag_Hidden) && mpShape != NULL) {
        prepareDraw();

        if (mpTexMtxObj != NULL) {
            J3DDifferedTexMtx::sTexGenBlock = mpShape->getMaterial()->getTexGenBlock();
            J3DDifferedTexMtx::sTexMtxObj = getTexMtxObj();
        } else {
            J3DDifferedTexMtx::sTexGenBlock = NULL;
        }

        if (mpSnapshot != NULL) {
            J3DNativeMaterial::sendDiffered(mpSnapshot);
        }

        mpShape->draw();
    }
}

void J3DShapePacket::drawFast() {
    if (!checkFlag(J3DShpFlag_Hidden) && mpShape != NULL) {
        prepareDraw();

        if (mpTexMtxObj != NULL) {
            J3DDifferedTexMtx::sTexGenBlock = mpShape->getMaterial()->getTexGenBlock();
            J3DDifferedTexMtx::sTexMtxObj = getTexMtxObj();
        } else {
            J3DDifferedTexMtx::sTexGenBlock = NULL;
        }

        mpShape->drawFast();
    }
}



int J3DMatPacket::entry(J3DDrawBuffer* pBuffer) {
    J3DDrawBuffer::sortFunc func = J3DDrawBuffer::sortFuncTable[pBuffer->getSortMode()];
    return (pBuffer->*func)(this);
}

