#pragma once

#include "JSystem/JAudio2/JASBasicWaveBank.hpp"
#include "JSystem/JAudio2/JASWaveInfo.hpp"
#include "Game/System/NebulaArena.hpp"

struct JASSimpleWaveBank : JASWaveBank, JASWaveArc {
    struct TWaveHandle : JASWaveHandle {
        TWaveHandle() : mArc(){};

        const u8* getWavePtr() const;
        const u8* getWaveLimit() const;
        const JASWaveInfo* getWaveInfo() const {
            return &mWaveInfo;
        }

        JASWaveInfo mWaveInfo;
        JASWaveArc* mArc;
    };

    JASSimpleWaveBank();
    ~JASSimpleWaveBank();
    void setWaveTableSize(u32, NebulaArena*);
    JASWaveHandle* getWaveHandle(u32) const;
    void setWaveInfo(u32, JASWaveInfo const&);
    JASWaveArc* getWaveArc(u32);
    u32 getArcCount() const {
        return 1;
    };

    TWaveHandle* mWaveTable;
    u32 mWaveTableSize;
};
