#pragma once

#include "Game/AudioLib/AudBgm.hpp"

class AudBgmKeeper {
public:
    enum BgmType {
        BgmType_Single = 0,
        BgmType_Multi = 1,
    };

    AudBgmKeeper();

    AudBgm* get(BgmType);
    void release(AudBgm*);
    AudSingleBgm* getValidSingleBgm();
    AudMultiBgm* getValidMultiBgm();

    AudSingleBgm mSingleBgm[2];
    AudMultiBgm mMultiBgm[2];
    u8 mSingleBgmActiveFlags;
    u8 mMultiBgmActiveFlags;
};
