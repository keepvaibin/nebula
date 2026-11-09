#pragma once

#include "Game/System/BinaryDataChunkHolder.hpp"

struct GameEventFlag;

namespace MR {
    class BitArray;
};  // namespace MR

class GameEventFlagStorage : public BinaryDataChunkBase {
public:
    GameEventFlagStorage();

    virtual u32 makeHeaderHashCode() const override;
    virtual u32 getSignature() const override;
    virtual s32 serialize(u8*, u32) const override;
    virtual s32 deserialize(const u8*, u32) override;
    virtual void initializeData() override;

    void set(const GameEventFlag*, bool);
    bool isOn(const GameEventFlag*) const;

    MR::BitArray* mFlagBitArray;
};
