#pragma once

#include "Game/Sound/Sound.hpp"
#include "Game/Sound/SoundInfo.hpp"
#include "Game/Sound/SoundGlobal.hpp"

class SoundTableInfo : public SoundGlobal< SoundTableInfo > {
public:
    enum DataType {
        DATA_SE = 0x50,
        DATA_BGM = 0x60,
        DATA_STREAM = 0x70,
    };

    SoundTableInfo(bool set) : SoundGlobal< SoundTableInfo >(set) {};

    virtual u16 getAudibleSw(SoundID) const = 0;
    virtual u16 getBgmSeqResourceID(SoundID) const = 0;
};
