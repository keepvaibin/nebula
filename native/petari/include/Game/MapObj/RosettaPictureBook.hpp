#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class IconAButton;
class PictureBookLayout;

class RosettaPictureBook : public LiveActor {
public:
    RosettaPictureBook(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;


    void exeWait();
    void exeDemoWait();
    void exeFadeOut();
    void exeReading();
    void exeFadeIn();

private:
    PictureBookLayout* mLayout;
    IconAButton* mIconAButton;
    bool mIsValidOpenIconAButton;
};
