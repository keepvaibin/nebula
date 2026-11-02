#pragma once

#include "JSystem/JAudio2/JASGadget.hpp"

struct JASVoice;

class JASChannel;
struct JASInstParam;
class JASWaveBank;

class JASBank {
public:
    static const int PRG_OSC = 240;

    JASBank() {
        mWaveBank = NULL;
    }
    virtual ~JASBank() {};
    virtual bool getInstParam(int, int, int, JASInstParam*) const = 0;
    virtual u32 getType() const = 0;

    JASWaveBank* getWaveBank() const {
        return mWaveBank;
    }
    void assignWaveBank(JASWaveBank* param_0) {
        mWaveBank = param_0;
    }

    static JASChannel* noteOn(JASBank const*, int, u8, u8, u16, void (*)(u32, JASChannel*, JASVoice*, void*), void*);
    static JASChannel* noteOnOsc(int, u8, u8, u16, void (*)(u32, JASChannel*, JASVoice*, void*), void*);

private:
    JASWaveBank* mWaveBank;
};
