#include "JSystem/JAudio2/JAUInitializer.hpp"

#include "JSystem/JAudio2/JAISe.hpp"
//
#include "JSystem/JAudio2/JAISeq.hpp"
//
#include "JSystem/JAudio2/JAIStream.hpp"
//
#include "JSystem/JAudio2/JAISoundChild.hpp"
//

#include "JSystem/JAudio2/JASStream.hpp"
#include "JSystem/JAudio2/JASDriverIF.hpp"
#include "JSystem/JAudio2/JASLoadThread.hpp"
#include "JSystem/JAudio2/JASTrack.hpp"
#include "JSystem/JAudio2/JASWaveArcLoader.hpp"
#include "Game/System/NebulaArena.hpp"

JAU_JASInitializer::JAU_JASInitializer() {
    heapSize_ = 0x1000;
    field_0x1c = 0x80;
    outputLevel_ = 1.0f;
    field_0x20 = 0;
    waveArcDir_ = nullptr;
}

JAU_JAIInitializer::JAU_JAIInitializer() {
    mMaxNumSe = 100;
    mMaxNumSeq = 4;
    mMaxNumStream = 2;
    mMaxNumChildren = 16;
}

void JAU_JAIInitializer::initJAInterface() {
    JAIStream::newMemPool(mMaxNumStream);
    JAISeq::newMemPool(mMaxNumSeq);
    JAISe::newMemPool(mMaxNumSe);
    JAISoundChild::newMemPool(mMaxNumChildren);
}
