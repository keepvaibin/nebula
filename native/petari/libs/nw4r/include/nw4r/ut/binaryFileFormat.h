#pragma once

#include <revolution.h>

namespace nw4r {
    namespace ut {
        typedef u32 SigWord;

        struct BinaryFileHeader {
            NEBULA_BE(SigWord) signature;
            NEBULA_BE(u16) byteOrder;
            NEBULA_BE(u16) version;
            NEBULA_BE(u32) fileSize;
            NEBULA_BE(u16) headerSize;
            NEBULA_BE(u16) dataBlocks;
        };

        struct BinaryBlockHeader {
            NEBULA_BE(SigWord) kind;
            NEBULA_BE(u32) size;
        };

        bool IsValidBinaryFile(const BinaryFileHeader *, u32, u16, u16);
    };
};
