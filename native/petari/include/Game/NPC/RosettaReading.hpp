#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class PictureBookLayout;

class RosettaReading : public LiveActor {
public:
    RosettaReading(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void appear();


    void exePictureBookBefore();
    void exePictureBook();
    void exePictureBookAfter();

private:
    PictureBookLayout* mPictureBookLayout;
};
