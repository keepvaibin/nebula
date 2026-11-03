#pragma once

#include "JSystem/JAudio2/JASVoice.hpp"

struct JASVoiceSlot {
    typedef s32 (*Callback)(u32, JASVoice*, void*);

    enum Status {
        /* 0 */ STATUS_ACTIVE,
        /* 1 */ STATUS_INACTIVE,
        /* 2 */ STATUS_DROP,
    };

    enum CallbackType {
        /* 0 */ CB_PLAY,
        /* 1 */ CB_START,
        /* 2 */ CB_STOP,
        /* 3 */ CB_DROP,
    };

    JASVoiceSlot();
    void free();
    void start();
    void drop();
    void setPriority(u8);
    void updateProc();
    void nameSound();
    u8 getStatus() const {
        return mStatus;
    }

    static void initAll();
    static JASVoiceSlot* alloc(u8, Callback, void*);
    static JASVoiceSlot* allocForce(u8, Callback, void*);
    static JASVoiceSlot* getLowestChannel(int);
    static void updateAll();
    static JASVoiceSlot* getHandle(u32);

    static JASVoiceSlot* sSlots;

    s32 mStatus;
    s16 mPriority;
    u32 mFlags;
    u32 field_0xc;
    Callback mCallback;
    void* mCallbackData;
    JASVoice* mChannel;
};
