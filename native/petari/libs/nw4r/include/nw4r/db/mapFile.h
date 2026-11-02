#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>
#include <revolution/os.h>

namespace nw4r {
    namespace db {
        bool MapFile_Exists(void);

        inline bool MapFile_QuerySymbol(u32 address, u8* strBuf, u32 strBufSize) {
            return false;
        }
    };  // namespace db
};  // namespace nw4r
