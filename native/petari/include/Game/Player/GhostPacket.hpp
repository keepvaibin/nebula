#pragma once

#include <nebula_geometry.hpp>

class GhostPacket {
public:
    GhostPacket(void*, u32);

    void read(u8*, u32);
    void read(u32*);
    void read(s16*);
    void read(char**);
    void read(s8*);
    void read(Vec3Sc*);
    void read(Vec3s*);

    u32 _0;
    u8* mDataPtr;
    u32 mCurOffs;
    u32 _C;
};
