#include "JSystem/J3DGraphAnimator/J3DModel.hpp"
#include "JSystem/J3DGraphAnimator/J3DJoint.hpp"
#include "JSystem/J3DGraphBase/J3DMaterial.hpp"
#include "JSystem/J3DGraphBase/J3DShapeMtx.hpp"

void J3DModel::initialize() {
    mModelData = NULL;
    mMotionEpoch = 0;
    mFlags = 0;
    mCalcCallBack = NULL;
    mUserArea = 0;

    mBaseScale.x = 1.0f;
    mBaseScale.y = 1.0f;
    mBaseScale.z = 1.0f;

    NebulaMtxIdentity(mBaseTransformMtx);
    NebulaMtxIdentity(mInternalView);

    mMtxBuffer = NULL;
    mMatPacket = NULL;
    mShapePacket = NULL;
    mMaterials = NULL;
    mUnkCalc1 = NULL;
}

s32 J3DModel::entryModelData(J3DModelData* pModelData, u32 mdlFlags, u32 mtxNum) {
    int ret = kJ3DError_Success;

    mModelData = pModelData;
    mMtxBuffer = new J3DMtxBuffer();

    ret = mMtxBuffer->create(pModelData, mtxNum);
    if (ret != kJ3DError_Success) {
        return ret;
    }

    ret = createShapePacket(pModelData);
    if (ret != kJ3DError_Success) {
        return ret;
    }

    ret = createMatPacket(pModelData);
    if (ret != kJ3DError_Success) {
        return ret;
    }

    mVertexBuffer.setVertexData(&pModelData->getVertexData());
    prepareShapePackets();

    if (mdlFlags & J3DMdlFlag_Unk1) {
        onFlag(J3DMdlFlag_Unk1);
    }
    if (mdlFlags & J3DMdlFlag_UseDefaultJ3D) {
        onFlag(J3DMdlFlag_UseDefaultJ3D);
    }

    return kJ3DError_Success;
}

s32 J3DModel::createShapePacket(J3DModelData* pModelData) {
    if (pModelData->getShapeNum() != 0) {
        mShapePacket = new J3DShapePacket[pModelData->getShapeNum()];

        for (int i = 0; i < pModelData->getShapeNum(); i++) {
            mShapePacket[i].setShape(pModelData->getShapeNodePointer(i));
            mShapePacket[i].setModel(this);
        }
    }

    return kJ3DError_Success;
}

s32 J3DModel::createMatPacket(J3DModelData* pModelData) {
    u16 matNum = pModelData->getMaterialNum();
    if (matNum != 0) {
        mMatPacket = new J3DMatPacket[matNum];
        mMaterials = new J3DMaterial*[matNum];
    }

    for (u16 i = 0; i < matNum; i++) {
        J3DMaterial* materialNode = pModelData->getMaterialNodePointer(i);
        J3DMatPacket* matPacket = mMatPacket + i;
        J3DShapePacket* shapePacket = mShapePacket + materialNode->getShape()->getIndex();

        mMaterials[i] = materialNode;
        shapePacket->mpMaterial = materialNode;
        matPacket->setMaterial(materialNode);
        matPacket->setInitShapePacket(shapePacket);
        matPacket->addShapePacket(shapePacket);
        matPacket->setMaterialID(materialNode->getID());

        if (pModelData->getModelDataType() == 1) {
            matPacket->lock();
        }
    }

    return kJ3DError_Success;
}

J3DMaterial* J3DModel::ownMaterial(u16 idx) {
    J3DMaterial* pShared = mModelData->getMaterialNodePointer(idx);
    if (mMaterials[idx] == pShared) {
        mMaterials[idx] = new J3DMaterial(*pShared);
        mShapePacket[pShared->getShape()->getIndex()].mpMaterial = mMaterials[idx];
    }
    return mMaterials[idx];
}

void J3DModel::lock() {
    int matNum = mModelData->getMaterialNum();
    for (int i = 0; i < matNum; i++) {
        mMatPacket[i].lock();
    }
}

void J3DModel::unlock() {
    int matNum = mModelData->getMaterialNum();
    for (int i = 0; i < matNum; i++) {
        mMatPacket[i].unlock();
    }
}

void J3DModel::calcAllMaterials() {
    j3dSys.setModel(this);
    u16 matNum = mModelData->getMaterialNum();
    for (u16 i = 0; i < matNum; i++) {
        J3DMaterial* material = mMaterials[i];
        material->calc(getAnmMtx(material->getJoint()->getJntNo()));
    }
}

void J3DModel::calcMaterial() {
    j3dSys.setModel(this);
    u16 matNum = mModelData->getMaterialNum();
    for (u16 i = 0; i < matNum; i++) {
        J3DMaterial* material = mMaterials[i];
        if (material != mModelData->getMaterialNodePointer(i)) {
            material->calc(getAnmMtx(material->getJoint()->getJntNo()));
        }
    }
}

void J3DModel::calcAnmMtx() {
    j3dSys.setModel(this);

    if (checkFlag(J3DMdlFlag_UseDefaultJ3D)) {
        getModelData()->getJointTree().calc(mMtxBuffer, j3dDefaultScale, j3dDefaultMtx);
    } else {
        getModelData()->getJointTree().calc(mMtxBuffer, mBaseScale, mBaseTransformMtx);
    }
}

void J3DModel::calcWeightEnvelopeMtx() {
    if (getModelData()->getWEvlpMtxNum() != 0) {
        if (!getModelData()->checkFlag(0x100)) {
            mMtxBuffer->calcWeightEnvelopeMtx();
        }
    }
}

void J3DModel::update() {
    calc();
    entry();
}

void J3DModel::calc() {
    j3dSys.setModel(this);
    mNebulaGeneration++;

    mModelData->syncJ3DSysFlags();
    mVertexBuffer.setCurrentVtxPos(mVertexBuffer.mVtxPosArray[0]);
    mVertexBuffer.setCurrentVtxNrm(mVertexBuffer.mVtxNrmArray[0]);
    mVertexBuffer.setCurrentVtxCol(mVertexBuffer.mVtxColArray[0]);

    if (mUnkCalc1 != NULL) {
        mUnkCalc1->calc(this);
    }

    calcAnmMtx();
    calcWeightEnvelopeMtx();

    if (mCalcCallBack != NULL) {
        mCalcCallBack(this, 0);
    }
}

void J3DModel::entry() {
    j3dSys.setModel(this);

    mModelData->syncJ3DSysFlags();
    j3dSys.setTexture(mModelData->getTexture());

    for (u16 i = 0; i < mModelData->getJointNum(); i++) {
        J3DJoint* joint = mModelData->getJointNodePointer(i);
        if (joint->getMesh() != NULL) {
            joint->entryIn();
        }
    }
}

void J3DModel::viewCalc() {
    mNebulaGeneration++;
    mMtxBuffer->swapDrawMtx();
    mMtxBuffer->swapNrmMtx();

    if (getModelData()->checkFlag(0x10)) {
        if (getMtxCalcMode() == 2) {
            J3DCalcViewBaseMtx(j3dSys.getViewMtx(), mBaseScale, mBaseTransformMtx, (MtxPtr)&mInternalView);
        }
    } else {
        mMtxBuffer->calcDrawMtx(getMtxCalcMode(), mBaseScale, mBaseTransformMtx);
        calcNrmMtx();
        calcBBoardMtx();
        calcBumpMtx();
    }

    prepareShapePackets();
}

void J3DModel::calcBumpMtx() {
    if (getModelData()->checkBumpFlag()) {
        u32 bumpMtxIdx = 0;
        u16 materialNum = getModelData()->getMaterialNum();

        for (u16 i = 0; i < materialNum; i++) {
            J3DMaterial* material = getModelData()->getMaterialNodePointer(i);
            if (material->getNBTScale()->mbHasScale == TRUE) {
                material->getShape()->calcNBTScale(*material->getNBTScale()->getScale(), getNrmMtxPtr(), getBumpMtxPtr(bumpMtxIdx));
                bumpMtxIdx++;
            }
        }
    }
}

void J3DModel::calcBBoardMtx() {
    if (getModelData()->checkBBoardFlag()) {
        mMtxBuffer->calcBBoardMtx();
    }
}

void J3DModel::prepareShapePackets() {
    u16 shapeNum = mModelData->getShapeNum();

    for (u16 i = 0; i < shapeNum; i++) {
        J3DShapePacket* shapePacket = &mShapePacket[i];
        shapePacket->setMtxBuffer(mMtxBuffer);

        if (getMtxCalcMode() == 2) {
            shapePacket->setBaseMtxPtr(&mInternalView);
        } else {
            shapePacket->setBaseMtxPtr((Mtx*)j3dSys.getViewMtx());
        }
    }
}

Mtx33* J3DModel::getNrmMtxPtr() {
    return mMtxBuffer->getNrmMtxPtr();
}

Mtx33* J3DModel::getBumpMtxPtr(int idx) {
    return mMtxBuffer->getBumpMtxPtr(idx);
}

inline void J3DModel::calcNrmMtx() {
    mMtxBuffer->calcNrmMtx();
}
