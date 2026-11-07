#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class NameObjArchiveListCollector;
class PictureBookLayout;

class RosettaReading : public LiveActor {
public:
    /// @brief Creates a new `RosettaReading`.
    /// @param pName A pointer to the null-terminated name of the object.
    RosettaReading(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void appear();
    virtual void kill();

    static void makeArchiveList(NameObjArchiveListCollector* pCollector, const TableIter& rIter);

    void exePictureBookBefore();
    void exePictureBook();
    void exePictureBookAfter();

private:
    PictureBookLayout* mPictureBookLayout;
};
