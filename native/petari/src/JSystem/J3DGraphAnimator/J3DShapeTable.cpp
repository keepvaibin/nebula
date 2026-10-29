#include "JSystem/J3DGraphAnimator/J3DShapeTable.hpp"

void J3DShapeTable::initShapeNodes(J3DDrawMtxData* pMtxData, J3DVertexData* pVtxData) {
    u16 shapeNum = mShapeNum;
    for (u16 i = 0; i < shapeNum; i++) {
        J3DShape* shapeNode = mShapeNodePointer[i];

        shapeNode->setDrawMtxDataPointer(pMtxData);
        shapeNode->setVertexDataPointer(pVtxData);
        shapeNode->initVtxAttr();
    }
}
