#pragma once

#include "JSystem/JAudio2/JAISound.hpp"

struct JAIStreamDataMgr {
    virtual s32 getStreamFileEntry(JAISoundID) = 0;
    virtual ~JAIStreamDataMgr();
};
