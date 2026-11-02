#pragma once


class GamePad;
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
    bool _E;
    const void* _10;
};

class GamePadRumble {
public:
    GamePadRumble(GamePad*);

    virtual ~GamePadRumble();

    void registInstance();
    void pause();
    void stop();
    void update();
    void updateRumble();
    GamePadRumble* getRumbleInstance() const;
    bool vibratePatternIfNotExist(const void*, const char*);
    bool setRumblePatternIfNotExist(const void*, const RumblePattern&, bool);
    bool findRubmlePattern(const void*, s32*, s32*, s32*, const RumblePattern&);

    GamePad* mPad;
    bool _8;
    u32 _C;
    RumbleChannel mChannel[8];
    s32 _B0;
    s32 _B4;
    bool _B8;
    s32 _BC;

private:
    static GamePadRumble** sInstanceForCallback;
};
