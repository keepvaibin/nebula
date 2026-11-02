#pragma once

#include "Game/Screen/LayoutActor.hpp"

class IconAButton : public LayoutActor {
public:
    IconAButton(bool, bool);

    virtual void init(const JMapInfoIter&);
    virtual void control();
    virtual void draw() const;

    void setFollowActorPane(LayoutActor*, const char*);
    bool isOpen();
    bool isWait();
    void open();
    void openWithTalk();
    void openWithRead();
    void openWithTurn();
    void openWithoutMessage();
    void term();
    void exeOpen();
    void exeWait();
    void exeTerm();
    void updateFollowPos();

    TVec2f mFollowPos;
    LayoutActor* mFollowActor;
    char mFollowPaneName[24];
};
