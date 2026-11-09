#pragma once

#include "Game/System/BinaryDataChunkHolder.hpp"

class StarPieceAlmsStorage : public BinaryDataChunkBase {
public:
    StarPieceAlmsStorage();

    virtual u32 makeHeaderHashCode() const override;
    virtual u32 getSignature() const override;
    virtual s32 serialize(u8*, u32) const override;
    virtual s32 deserialize(const u8*, u32) override;
    virtual void initializeData() override;

    s32 getValue(int) const;
    s32 getMaxValue(int) const;
    void addValue(int, int);

private:
    u16* mValueArray;
    // The slots the array was made with; an index outside them reads 0 and adds nothing.
    s32 mValueNum;
};
