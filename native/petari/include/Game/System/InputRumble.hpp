#pragma once


class PlayerInput;
struct RumblePattern;

class RumbleChannel {
public:
    void clear();
    void update();
    void setPattern(const void*, const RumblePattern&, u32, bool);

    const RumblePattern* _0;
    bool _4;
    u32 _8;
    s16 _C;
    u8 mStrength;
    const void* _10;
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
    u32 _C;
    RumbleChannel mChannel[8];
    s32 _BC;

private:
    static InputRumble** sInstanceForCallback;
};
