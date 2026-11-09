#pragma once

#include "Game/System/BinaryDataChunkHolder.hpp"

// The save's lives and stocked star bits.
class GameDataPlayerStatus : public BinaryDataChunkBase {
public:
    GameDataPlayerStatus();

    virtual u32 makeHeaderHashCode() const override;
    virtual u32 getSignature() const override;
    virtual s32 serialize(u8* pBuffer, u32 size) const override;
    // 0 or 1 on success.
    virtual s32 deserialize(const u8* pBuffer, u32 size) override;
    virtual void initializeData() override;

    s32 getPlayerLeft() const;
    void addPlayerLeft(int num);
    // Whether the last session earned enough lives to start with them.
    bool isPlayerLeftSupply() const;
    void offPlayerLeftSupply();
    void addStockedStarPiece(int num);

    u16 mPlayerLeft;
    u32 mStockedStarPiece;
    u8 mStoryProgress;

private:
    // The lives from the previous session.
    u16 mPlayerLeftSupply;
};
