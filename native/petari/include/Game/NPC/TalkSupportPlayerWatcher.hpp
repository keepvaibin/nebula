#pragma once


class TalkSupportPlayerWatcher {
public:
    /// @brief Creates a new `TalkSupportPlayerWatcher`.
    TalkSupportPlayerWatcher();

    void update();
    bool isEnableTalkPlayerStateNormal() const;
    bool isEnableTalkPlayerStateEvent() const;
    bool isEnableTalkPlayerSpeed() const;

    u32 mEnableTalkCounter;
    u32 mDisableTalkCounter;
};
