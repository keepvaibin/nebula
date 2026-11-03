#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class GhostPacket {
public:
    GhostPacket(void*, u32);

    void read(u8*, u32);
    void read(u32*);
    void read(s16*);
    void read(char**);
    void read(s8*);
    void read(TVec3Sc*);
    void read(TVec3s*);

    u32 _0;
    u8* mDataPtr;
    u32 mCurOffs;
    u32 _C;
};
