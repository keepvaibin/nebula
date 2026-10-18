#include "Game/Player/GhostPacket.hpp"
#include <JSystem/JGeometry/TVec.hpp>
#include <cstdio>

GhostPacket::GhostPacket(void* pData, u32 len) {
    mDataPtr = (u8*)pData;
    mCurOffs = 0;
    _C = len;
}

void GhostPacket::read(u8* pOut, u32 len) {
    for (int i = 0; i < len; i++) {
        *pOut = mDataPtr[mCurOffs];
        pOut++;
        mCurOffs++;
    }
}

// Ghost data is big-endian: the byte copies below are the console's in-place reads; natively the copied value is then
// put in host order.
void GhostPacket::read(u32* pOut) {
    read((u8*)pOut, 4);
#ifdef TARGET_PC
    *pOut = __builtin_bswap32(*pOut);
#endif
}

void GhostPacket::read(s16* pOut) {
    read((u8*)pOut, 2);
#ifdef TARGET_PC
    *pOut = (s16)__builtin_bswap16((u16)*pOut);
#endif
}

void GhostPacket::read(char** pOut) {
    char* v3 = (char*)&mDataPtr[mCurOffs];
    *pOut = (char*)v3;
    s32 offs = strlen(v3) + 1;
    mCurOffs += offs;
}

void GhostPacket::read(s8* pOut) {
    read((u8*)pOut, 1);
}

void GhostPacket::read(TVec3Sc* pOut) {
    read((u8*)&pOut->x, 1);
    read((u8*)&pOut->y, 1);
    read((u8*)&pOut->z, 1);
}

void GhostPacket::read(TVec3s* pOut) {
#ifdef TARGET_PC
    read(&pOut->x);
    read(&pOut->y);
    read(&pOut->z);
#else
    read((u8*)&pOut->x, 2);
    read((u8*)&pOut->y, 2);
    read((u8*)&pOut->z, 2);
#endif
}
