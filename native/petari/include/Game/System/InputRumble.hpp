#pragma once

#include "Game/Util/Array.hpp"

class PlayerInput;
struct RumblePattern;

class RumbleChannel {
public:
    void clear();
    void update();
    void setPattern(const void* pOwner, const RumblePattern&, u32 serial, bool isLoop);

    const RumblePattern* mPattern;
    bool mIsLoop;
    u32 mSerial;
    s16 mStep;
    u8 mStrength;
    const void* mOwner;
};

class InputRumble {
public:
    InputRumble(PlayerInput*);

    virtual ~InputRumble();

    void registInstance();
    void pause();
    void stop();
    void update();
    void updateRumble();
    InputRumble* getRumbleInstance() const;
    bool vibratePatternIfNotExist(const void* pOwner, const char* pPatternName);
    bool setRumblePatternIfNotExist(const void* pOwner, const RumblePattern&, bool isLoop);
    bool findRumblePattern(const void* pOwner, s32* pExisting, s32* pEmpty, const RumblePattern&);

    PlayerInput* mPad;
    u8 mStrength;
    u32 mSerial;
    // One per pattern playing at once: a channel whose pattern ended is used again, and more are added past these.
    MR::InlineVector< RumbleChannel, 8 > mChannels;
    s32 mStopTimer;

private:
    static InputRumble** sInstanceForCallback;
};
