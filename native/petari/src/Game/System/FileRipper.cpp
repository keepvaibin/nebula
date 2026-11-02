#include "Game/System/FileRipper.hpp"
#include "Game/System/GameThreads.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include <JSystem/JKernel/JKRDvdRipper.hpp>
#include <JSystem/JKernel/JKRHeap.hpp>
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/NebulaDisc.hpp"
#include "JSystem/JUtility/JUTException.hpp"

void* FileRipper::loadToMainRAM(const char* fpath, u8* dest, JKRHeap* pHeap, AllocDirection allocDir) {
    bool alloced = false;

    if (!MR::isFileExist(fpath, false)) {
        JUTException::panic_f(__FILE__, 0x70, "File isn't exist.");
    }

    u32 fsize = (MR::getFileSize(fpath, false) + 0x1f) / 0x20 * 0x20;

    NebulaDisc::File fileInfo;
    if (!NebulaDisc::open(fpath, &fileInfo)) {
        JUTException::panic_f(__FILE__, 0x77, "NebulaDisc::open() failed");
    }

    if (!dest) {
        int align = allocDir == 0 ? 0x40 : -0x40;
        dest = (u8*)JKRHeap::alloc(fsize, align, pHeap);
        alloced = true;
    }
    if (!dest) {
        return 0;
    }

    while (true) {
        s32 result = NebulaDisc::read(&fileInfo, dest, fsize, 0);
        if (result >= 0) {
            break;
        }
        if (result == -3) {
            if (alloced) {
                JKRHeap::free(dest, 0);
            }
            return 0;
        }
        NebulaFrameClock::waitFrame();
    }

    JKRDvdRipper::panicIfCompressed(dest, fpath);
    return dest;
}
