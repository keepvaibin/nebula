#pragma once

#include "Game/Demo/DemoCastGroup.hpp"
#include "Game/Scene/MovementControlType.hpp"
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
    virtual void initCast(LiveActor*, const TableIter&) {};
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

    virtual void init(const TableIter& rIter);
    virtual void movement();

    virtual void registerDemoActor(LiveActor*, const TableIter&);

    void start(NameObj*, const char*, MR::MovementControlType);
    void startPart(NameObj*, const char*, const char*, MR::MovementControlType);
    void startProperDemoSystem();
    void startDemoSystemPart(const char*, MR::MovementControlType);
    bool tryStartProperDemoSystem();
    bool tryStartDemoSystemPart(const char*, MR::MovementControlType);
    bool tryStartProperDemoSystemPart(const char* pPartName);
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
    MR::InlineVector< DemoSheetKeeperBase*, 2 > mSheetKeeper;
    StageSwitchCtrl* mStageSwitchCtrl;
    NameObj* mDemoStarter;
    const char* mDemoName;
    MR::MovementControlType mMovementType;  // None while not running
    MR::InlineVector< LiveActor*, 192 > mUnclippedCasts;  // casts start() stopped clipping; end() restores them
    MR::InlineVector< DemoTalkAnimCtrl*, 8 > mTalkAnimCtrl;
    MR::InlineVector< DemoTalkMessageCtrl, 8 > mTalkMessageCtrl;
};
