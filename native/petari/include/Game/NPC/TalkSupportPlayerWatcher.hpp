#pragma once


class TalkSupportPlayerWatcher {
public:
    TalkSupportPlayerWatcher();

    void update();
    bool isEnableTalkPlayerStateNormal() const;
    bool isEnableTalkPlayerStateEvent() const;
    bool isEnableTalkPlayerSpeed() const;

};
