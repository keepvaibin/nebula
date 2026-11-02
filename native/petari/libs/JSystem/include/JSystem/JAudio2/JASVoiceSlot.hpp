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
    static JASVoiceSlot* getLowestActiveChannel();
    static void updateAll();
    static int killActiveChannel();
    static JASVoiceSlot* getHandle(u32);
    static u32 getNumUse();
    static u32 getNumFree();
    static u32 getNumBreak();

    static JASVoiceSlot* sSlots;

    /* 0x00 */ s32 mStatus;
    /* 0x04 */ s16 mPriority;
    /* 0x08 */ u32 mFlags;
    /* 0x0C */ u32 field_0xc;
    /* 0x10 */ Callback mCallback;
    /* 0x14 */ void* mCallbackData;
    /* 0x18 */ JASVoice* mChannel;
};
