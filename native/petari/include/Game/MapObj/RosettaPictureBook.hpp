#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class IconAButton;
class PictureBookLayout;

class RosettaPictureBook : public LiveActor {
public:
    RosettaPictureBook(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void appear();
    virtual void kill();
    virtual void control();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);


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
