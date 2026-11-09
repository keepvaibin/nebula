#pragma once

#include "Game/Screen/LayoutActor.hpp"
#include "Game/Util/StringUtil.hpp"

class IconAButton : public LayoutActor {
public:
    IconAButton(bool, bool);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual void draw() const override;

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

    Vec2f mFollowPos;
    LayoutActor* mFollowActor;
    MR::FormattedName mFollowPaneName;
};
