#pragma once

#include "Game/Demo/DemoCastGroup.hpp"
#include "Game/Util/Array.hpp"

class DemoActionKeeper;
class DemoCameraKeeper;
class DemoExecutor;
class DemoPlayerKeeper;
class DemoSoundKeeper;
class DemoSubPartInfo;
class DemoSubPartKeeper;
class DemoTalkAnimCtrl;
class DemoTimeKeeper;
class DemoWipeKeeper;
class StageSwitchCtrl;
class TalkMessageCtrl;

class DemoSheetKeeperBase {
public:
    DemoSheetKeeperBase(DemoExecutor* pExecutor) : mExecutor(pExecutor) {
    }

    virtual const char* getName() const = 0;
    virtual const char* getTypeString() const = 0;
    virtual void initCast(LiveActor*, const JMapInfoIter&) {};
    virtual void start() {
    }

    virtual void end() {
    }

    virtual void update() {
    }

    DemoExecutor* mExecutor;
};

template < class T >
class DemoSheetKeeperInfoHolder {
public:
    virtual void executeType(const T* pInfo);

    void update() {
        for (T* pInfo = mInfo.begin(); pInfo != mInfo.end(); pInfo++) {
            executeType(pInfo);
        }
    }

    MR::Vector< MR::AssignableArray< T > > mInfo;
};

struct DemoTalkMessageCtrl {
    LiveActor* mActor;
    TalkMessageCtrl* mTalkMessageCtrl;
};

class DemoExecutor : public DemoCastGroup {
public:
    /// @brief Creates a new `DemoExecutor`
    DemoExecutor(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void movement();

    virtual void registerDemoActor(LiveActor*, const JMapInfoIter&);

    void start(NameObj*, const char*, s32);
    void startPart(NameObj*, const char*, const char*, s32);
    void startProperDemoSystem();
    void startDemoSystemPart(const char*, s32);
    bool tryStartProperDemoSystem();
    bool tryStartDemoSystemPart(const char*, s32);
    bool tryStartProperDemoSystemPart(const char*);
    void pause();
    void resume();
    void addTalkAnimCtrl(DemoTalkAnimCtrl*);
    void addTalkMessageCtrl(LiveActor*, TalkMessageCtrl*);
    TalkMessageCtrl* findTalkMessageCtrl(const LiveActor*) const;
    void setTalkMessageCtrl(const LiveActor*, TalkMessageCtrl*);
    void end();

    const char* mSheetName;
    DemoTimeKeeper* mTimeKeeper;
    DemoSubPartKeeper* mSubPartKeeper;
    DemoPlayerKeeper* mPlayerKeeper;
    DemoCameraKeeper* mCameraKeeper;
    DemoActionKeeper* mActionKeeper;
    DemoWipeKeeper* mWipeKeeper;
    DemoSoundKeeper* mSoundKeeper;
    MR::Vector< MR::FixedArray< DemoSheetKeeperBase*, 2 > > mSheetKeeper;
    StageSwitchCtrl* _40;
    NameObj* _44;
    const char* _48;
    s32 _4C;
    bool _50;
    MR::Vector< MR::FixedArray< LiveActor*, 192 > > mActor;
    MR::Vector< MR::FixedArray< DemoTalkAnimCtrl*, 8 > > mTalkAnimCtrl;
    MR::Vector< MR::FixedArray< DemoTalkMessageCtrl, 8 > > mTalkMessageCtrl;
};
