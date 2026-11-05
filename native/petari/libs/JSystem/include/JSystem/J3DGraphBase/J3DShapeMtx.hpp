#pragma once

#include "JSystem/J3DGraphBase/J3DShape.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

// A group of more than one matrix: its table (0xFFFF: the group before's stays).
class J3DShapeMtxMulti : public J3DShapeMtx {
public:
    J3DShapeMtxMulti(u16 useMtxIndex, u16 useMtxNum, u16* useMtxIndexTable)
        : J3DShapeMtx(useMtxIndex), mUseMtxNum(useMtxNum), mUseMtxIndexTable(useMtxIndexTable) {
    }

    virtual ~J3DShapeMtxMulti() {
    }
    virtual bool isMulti() const {
        return true;
    }
    virtual u16 getUseMtxNum() const {
        return mUseMtxNum;
    }
    virtual u16 getUseMtxIndex(u16 no) const {
        return mUseMtxIndexTable[no];
    }

protected:
    u16 mUseMtxNum;
    u16* mUseMtxIndexTable;
};
