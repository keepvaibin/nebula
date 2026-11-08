#pragma once

#include "Game/System/BinaryDataChunkHolder.hpp"

class StarPieceAlmsStorage : public BinaryDataChunkBase {
public:
    StarPieceAlmsStorage();

    virtual u32 makeHeaderHashCode() const;
    virtual u32 getSignature() const;
    virtual s32 serialize(u8*, u32) const;
    virtual s32 deserialize(const u8*, u32);
    virtual void initializeData();

    s32 getValue(int) const;
    s32 getMaxValue(int) const;
    void addValue(int, int);

private:
    u16* mValueArray;
};
