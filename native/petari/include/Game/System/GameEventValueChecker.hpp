#pragma once

#include "Game/System/BinaryDataChunkHolder.hpp"

struct GameEventValue {
    const char* mName;
    u16 mDefaultValue;
};

class GameEventValueChecker : public BinaryDataChunkBase {
public:
    GameEventValueChecker();

    virtual u32 makeHeaderHashCode() const;
    virtual u32 getSignature() const;
    virtual s32 serialize(u8*, u32) const;
    virtual s32 deserialize(const u8*, u32);
    virtual void initializeData();

    u32 getValue(const char*) const;
    void setValue(const char*, u16);

    s32 findIndex(const char*) const;
    s32 findIndexFromHashCode(u16) const;

    u16* mValues;
    s32 mNumValues;
};
