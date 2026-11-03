#pragma once

#include "Game/Screen/LayoutActor.hpp"

class GalaxyInfoLayoutSetter;
class NameObjArchiveListCollector;
class ValueControl;

class GalaxySelectInfo : public LayoutActor {
public:
    GalaxySelectInfo(bool);

    virtual void init(const JMapInfoIter& rIter);
    virtual void draw() const;
    virtual void appear();
    virtual void kill();
    virtual void control();

    bool show(const char*, u32, int);
    static void makeArchiveList(NameObjArchiveListCollector* pCollector, const JMapInfoIter& rIter);
    bool show(const char*, u32, bool, int);
    void exeWait();
    void exeDisappear();

    bool _20;
    const char* _24;
    bool _28;
    ValueControl* mFadeinoutControl;
    GalaxyInfoLayoutSetter* mInfoLayoutSetter;
};
