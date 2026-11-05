#pragma once

#include "JSystem/JAudio2/JAUAudioArcInterpreter.hpp"

class JAUAudioData;

class JAUAudioArcLoader : public JAUAudioArcInterpreter {
public:
    explicit JAUAudioArcLoader(JAUAudioData*);
    bool load(void const*);
    bool load(char const*);
    virtual void readWS(u32, void const*, u32);
    virtual void readBNK(u32, void const*);
    virtual void readBSC(void const*, u32);
    virtual void readBST(void const*, u32);
    virtual void readBSTN(void const*, u32);
    virtual void readBMS(u32, void const*, u32);
    virtual void readBMS_fromArchive(u32);
    virtual void newVoiceBank(u32, u32);
    virtual void newDynamicSeqBlock(u32);
    virtual void readBSFT(void const*);
    virtual void beginBNKList(u32, u32);
    virtual void endBNKList();
    virtual void readMaxSeCategory(int, int, int);
    virtual ~JAUAudioArcLoader() {
    }

    JAUAudioData* mData;
};
