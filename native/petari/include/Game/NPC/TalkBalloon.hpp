#pragma once

#include "Game/Screen/LayoutActor.hpp"

class TalkMessageCtrl;
class TalkTextFormer;
class TableIter;
class IconAButton;

class TalkBalloon : public LayoutActor {
public:
    TalkBalloon(const char*);

    virtual void init(const TableIter&) override;
    virtual void kill() override;

    virtual void open(TalkMessageCtrl*);
    virtual void close();
    virtual void updateBalloon();
    virtual void updateTalking();
    virtual void pauseOff();
    virtual bool turnPage();
    virtual bool hasNextPage();
    virtual void skipMessage();
    virtual bool isTextAppearedAll();

    void create(const char* pLayoutName, bool hasBeak, bool isTalkLayout);

    TalkMessageCtrl* mMessageCtrl;
    TalkTextFormer* mTextFormer;
    bool mHasBeak;
    bool mIsBeakMoving;
};

class TalkBalloonShort : public TalkBalloon {
public:
    TalkBalloonShort(const char*);

    virtual void init(const TableIter&) override;
    virtual void open(TalkMessageCtrl*) override;
    virtual void close() override;
    virtual void updateBalloon() override;

    void initInterval();
    void exeTalk();
    void exeClose();
    void exeOpen();

    u32 mInterval;
};

class TalkBalloonEvent : public TalkBalloon {
public:
    TalkBalloonEvent(const char*);

    virtual void init(const TableIter&) override;
    virtual void open(TalkMessageCtrl*) override;
    virtual void close() override;
    virtual bool turnPage() override;
    virtual void skipMessage() override;
    virtual bool isTextAppearedAll() override;

    void initInterval();
    void exeTalk();
    void exeWait();
    void exeOpen();
    void updateBeak();
    void exeClose();

    u32 mFormMode;
    IconAButton* mAButton;
};

class TalkBalloonSign : public TalkBalloonEvent {
public:
    TalkBalloonSign(const char*);

    virtual void init(const TableIter&) override;
};

class TalkBalloonInfo : public TalkBalloon {
public:
    TalkBalloonInfo(const char*);

    virtual void open(TalkMessageCtrl*) override;
    virtual void close() override;
    virtual bool isTextAppearedAll() override {
        return true;
    }
    virtual void updateBalloon() override {
    }
    virtual void updateTalking() override {
    }
};

class TalkBalloonIcon : public TalkBalloonShort {
public:
    TalkBalloonIcon(const char*);

    virtual void init(const TableIter&) override;
    virtual void open(TalkMessageCtrl*) override;
};

class TalkBalloonHolder {
public:
    TalkBalloonHolder();

    void pauseOff();
    void skipMessage();
    TalkBalloon* getBalloon(const TalkMessageCtrl*);
    void balloonOff();
    void update();
    bool isActiveBalloonShort() const;

    TalkBalloonShort** mBalloonShortArray;
    TalkBalloonEvent* mBalloonEvent;
    TalkBalloonInfo* mBalloonInfo;
    TalkBalloonSign* mBalloonSign;
    TalkBalloonIcon* mBalloonIcon;
    s32 mShortIndex;
    IconAButton* mAButton;
};

namespace MR {
    void recoverSoundVolumeSetting(u32);
    bool isCameraInterpolatingNearlyEnd();
};  // namespace MR
