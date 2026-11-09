#pragma once

#include "Game/Screen/LayoutActor.hpp"

class IconAButton;

class ProloguePictureBook : public LayoutActor {
public:
    ProloguePictureBook();

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;

    void exeActive();
    void exePlaying();
    void exeKeyWait();
    void exeEnd();
    bool isEnd() const;

private:
    IconAButton* mIconAButton;
    u32 mPage;
};
