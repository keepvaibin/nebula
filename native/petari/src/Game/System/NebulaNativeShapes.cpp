#include "Game/System/NebulaNativeShapes.hpp"

#ifdef TARGET_PC
#include "JSystem/J3DGraphAnimator/J3DModelData.hpp"
#include "JSystem/J3DGraphBase/J3DShape.hpp"
#include "JSystem/J3DGraphBase/J3DShapeDraw.hpp"
#include "JSystem/J3DGraphBase/J3DVertex.hpp"
#include <revolution/gx.h>
#include <revolution/os.h>

namespace NebulaNativeShapes {
    void registerModel(J3DModelData* pModelData) {
        if (pModelData == nullptr) {
            return;
        }
        J3DVertexData& rVertex = pModelData->getVertexData();
        for (u16 s = 0; s < pModelData->getShapeNum(); s++) {
            J3DShape* pShape = pModelData->getShapeNodePointer(s);
            if (pShape == nullptr || pShape->getVcdVatCmd() == nullptr) {
                continue;
            }
            // As J3DShape::loadVtxArray loads them: the normal array only for a shape that is not bump-mapped.
            u32 arrays[3];
            arrays[0] = OSCachedToPhysical(rVertex.getVtxPosArray());
            arrays[1] = pShape->getNBTFlag() ? 0 : OSCachedToPhysical(rVertex.getVtxNrmArray());
            arrays[2] = OSCachedToPhysical(rVertex.getVtxColorArray(0));
            const J3DCurrentMtx& rMtx = pShape->getCurrentMtx();
            for (u16 g = 0; g < pShape->getMtxGroupNum(); g++) {
                const J3DShapeDraw* pDraw = pShape->getShapeDraw(g);
                if (pDraw == nullptr || pDraw->getNebulaHandle() == 0 || (pDraw->getDisplayListSize() & 31) != 0) {
                    continue;
                }
                __NebulaNativeShape(pDraw->getNebulaHandle(), pDraw->getDisplayList(),
                                    GX_PHY_ADDR(pDraw->getDisplayList()), pDraw->getDisplayListSize(),
                                    pShape->getVcdVatCmd(), J3DShape::kVcdVatDLSize, arrays, rMtx.mMtxIdxRegA,
                                    rMtx.mMtxIdxRegB);
            }
        }
    }
};  // namespace NebulaNativeShapes
#endif
