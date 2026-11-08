#pragma once


class PlayerInput;
struct RumblePattern;

class RumbleChannel {
public:
    void clear();
    void update();
    void setPattern(const void*, const RumblePattern&, u32, bool);

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
    bool vibratePatternIfNotExist(const void*, const char*);
    bool setRumblePatternIfNotExist(const void*, const RumblePattern&, bool);
    bool findRubmlePattern(const void*, s32*, s32*, s32*, const RumblePattern&);

    PlayerInput* mPad;
    u8 mStrength;
    u32 mSerial;
    RumbleChannel mChannel[8];
    s32 mStopTimer;

private:
    static InputRumble** sInstanceForCallback;
};
