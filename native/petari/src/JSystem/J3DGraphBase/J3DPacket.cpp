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
}

J3DDrawPacket::~J3DDrawPacket() {
}

void J3DDrawPacket::draw() {
}

J3DMatPacket::J3DMatPacket() {
    mpShapePacket = NULL;
    mpInitShapePacket = NULL;
    mpMaterial = NULL;
    mMaterialID = 0xFFFFFFFF;
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
    return mMaterialID == pOther->mMaterialID && (mMaterialID >> 31) == 0;
}

void J3DMatPacket::draw() {
    mpMaterial->load();
    J3DNativeMaterial::send(mpMaterial);

    J3DShapePacket* packet = getShapePacket();
    packet->getShape()->loadPreDrawSetting();

    while (packet != NULL) {
        J3DNativeMaterial::sendOwn(packet->mpMaterial, mpMaterial);
        packet->drawFast();
        packet = (J3DShapePacket*)packet->getNextPacket();
    }
}



J3DShapePacket::J3DShapePacket() {
    mpShape = NULL;
    mpMtxBuffer = NULL;
    mpBaseMtxPtr = NULL;
    mpModel = NULL;
    mpMaterial = NULL;
}

J3DShapePacket::~J3DShapePacket() {
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
        J3DNativeMaterial::sendOwn(mpMaterial, mpShape->getMaterial());
        mpShape->draw();
    }
}

void J3DShapePacket::drawFast() {
    if (!checkFlag(J3DShpFlag_Hidden) && mpShape != NULL) {
        prepareDraw();
        mpShape->drawFast();
    }
}



int J3DMatPacket::entry(J3DDrawBuffer* pBuffer) {
    J3DDrawBuffer::sortFunc func = J3DDrawBuffer::sortFuncTable[pBuffer->getSortMode()];
    return (pBuffer->*func)(this);
}

