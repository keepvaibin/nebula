#pragma once


namespace AudSpeakerWrap {
    const char* getResName();
    u32 getRequiredHeapSize();
    void start(s32, s32);
    bool isPlayable(s32);
};  // namespace AudSpeakerWrap
