#include "Game/System/NebulaCurvedSurfaces.hpp"

#include "JSystem/J3DGraphAnimator/J3DModelData.hpp"
#include "JSystem/J3DGraphBase/J3DShape.hpp"
#include "JSystem/J3DGraphBase/J3DShapeDraw.hpp"
#include "JSystem/J3DGraphBase/J3DVertex.hpp"
#include <nebula_game_api.h>
#include <NebulaGpu.h>
#include <stdlib.h>
#include <string.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // A growable array in the CRT's heap: the arrays here are CPU-only and short-lived, and the game's heaps are sized
    template < typename T >
    struct Grow {
        T* mData;
        u32 mCount;
        u32 mCapacity;

        Grow() : mData(nullptr), mCount(0), mCapacity(0) {}
        ~Grow() { free(mData); }

        bool push(const T& rValue) {
            if (mCount == mCapacity) {
                const u32 capacity = mCapacity != 0 ? mCapacity * 2 : 256;
                T* pData = static_cast< T* >(realloc(mData, capacity * sizeof(T)));
                if (pData == nullptr) {
                    return false;
                }
                mData = pData;
                mCapacity = capacity;
            }
            mData[mCount++] = rValue;
            return true;
        }
    };
};  // namespace

namespace NebulaCurvedSurfaces {
    void registerModel(J3DModelData* pModelData) {
        if (pModelData == nullptr) {
            return;
        }
        J3DVertexData& rVertex = pModelData->getVertexData();
        const f32* pPositions = static_cast< const f32* >(rVertex.getVtxPosArray());
        const f32* pNormals = static_cast< const f32* >(rVertex.getVtxNrmArray());
        if (pPositions == nullptr || pNormals == nullptr || rVertex.getNormalFloats() != 3) {
            return;
        }
        Grow< u16 > corners;
        u16 maxPosition = 0;
        u16 maxNormal = 0;
        for (u16 s = 0; s < pModelData->getShapeNum(); s++) {
            J3DShape* pShape = pModelData->getShapeNodePointer(s);
            // Bump-mapped shapes carry their tangents where the renderer would put the displacement's curve data.
            if (pShape == nullptr || pShape->getNBTFlag()) {
                continue;
            }
            for (u16 g = 0; g < pShape->getMtxGroupNum(); g++) {
                J3DShapeMesh* pMesh = pShape->getShapeDraw(g)->getMesh();
                if ((pMesh->mAttributes & NEBULA_SHAPE_NORMAL) == 0) {
                    continue;
                }
                const NebulaShapeCorner* pCorners = pMesh->corners();
                const u16* pIndices = pMesh->indices();
                for (u32 i = 0; i < pMesh->mIndexCount; i++) {
                    const NebulaShapeCorner& rCorner = pCorners[pIndices[i]];
                    if (!corners.push(rCorner.position) || !corners.push(rCorner.normal)) {
                        return;
                    }
                    maxPosition = rCorner.position > maxPosition ? rCorner.position : maxPosition;
                    maxNormal = rCorner.normal > maxNormal ? rCorner.normal : maxNormal;
                }
            }
        }
        if (corners.mCount == 0) {
            return;
        }
        const NebulaHostServices* pHost = nebula_host();
        pHost->curved_surface(pHost->user, static_cast< u64 >(reinterpret_cast< uintptr_t >(pPositions)), pPositions,
                              maxPosition + 1u, pNormals, maxNormal + 1u, corners.mData, corners.mCount / 6);
    }
};  // namespace NebulaCurvedSurfaces
