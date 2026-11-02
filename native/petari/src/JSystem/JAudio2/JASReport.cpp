#include "JSystem/JAudio2/JASReport.hpp"
#include "Game/System/GameThreads.hpp"
#include "JSystem/JAudio2/JASCalc.hpp"
#include "JSystem/JKernel/JKRHeap.hpp"
#include "Game/System/GameThreads.hpp"
#include <cstdio>
#include "Game/System/GameThreads.hpp"

static GameMutex sMutex;
static char* sBuffer;
static int sLineMax;
static int sLineCount;
static int sTop;

void JASReport(const char* pFormat, ...) {
    va_list vl;
    if (sBuffer == nullptr) {
        return;
    }

    va_start(vl, pFormat);
    GameMutex* pMutex = &sMutex;
    pMutex->lock();
    nebula_vsnprintf(sBuffer + (sTop * 64), 64, pFormat, vl);
    va_end(vl);

    sTop++;
    if (sTop >= sLineMax) {
        sTop = 0;
    }

    if (sLineCount < sLineMax) {
        sLineCount++;
    }

    pMutex->unlock();
}
