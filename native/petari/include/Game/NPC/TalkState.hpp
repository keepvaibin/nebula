#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

class TalkMessageCtrl;
class TalkBalloon;
class LiveActor;
class TalkSupportPlayerWatcher;
class IconAButton;
class LayoutActor;
class TalkBalloonShort;
class TalkBalloonEvent;
class TalkBalloonInfo;
class TalkBalloonSign;
class TalkBalloonCompose;

class TalkState {
public:
    TalkState();

    virtual void init(TalkMessageCtrl*, TalkBalloon*);
    virtual bool prep(const TalkMessageCtrl*) {
        return true;
    }

    virtual bool test() {
        return true;
    }

    virtual void open() {
    }

    virtual bool talk(const TalkMessageCtrl*) {
        return true;
    }

    virtual void clos() {
    }

    virtual bool term(const TalkMessageCtrl*) {
        return true;
    }

    virtual u32 getPageCount() const;

    void balloonOff() const;
    bool isLostMessage(const TalkMessageCtrl*) const;
    bool isSelfInterrupt(const TalkMessageCtrl*) const;

    TalkMessageCtrl* mMsgCtrl;
    TalkBalloon* mBalloon;
    u32 mMessageID;
};

class TalkStateShort : public TalkState {
public:
    TalkStateShort();

    virtual bool prep(const TalkMessageCtrl*) override;
    virtual void open() override;
    virtual bool talk(const TalkMessageCtrl*) override;
    virtual void clos() override;
};

class TalkStateEvent : public TalkState {
public:
    TalkStateEvent();

    virtual bool prep(const TalkMessageCtrl*) override;
    virtual bool test() override;
    virtual void open() override;
    virtual bool talk(const TalkMessageCtrl*) override;
    virtual void clos() override;
    virtual bool term(const TalkMessageCtrl*) override;
    virtual u32 getPageCount() const override;

    s32 mSkipHoldStep;
    s32 mTermStep;
    TalkSupportPlayerWatcher* mPlayerWatcher;
    u8 mIsJumpHeld;
    u32 mPageCount;
};

class TalkStateNormal : public TalkStateEvent {
public:
    TalkStateNormal();

    virtual bool prep(const TalkMessageCtrl*) override;
    virtual bool test() override;
    virtual void clos() override;
    virtual bool term(const TalkMessageCtrl*) override;
    void updateButton();

    IconAButton* mAButton;
};

class TalkStateCompose : public TalkStateNormal {
public:
    TalkStateCompose();

    virtual void init(TalkMessageCtrl*, TalkBalloon*) override;
    virtual bool prep(const TalkMessageCtrl*) override;
    virtual bool test() override;
    virtual void open() override;

    TalkBalloon* mSecondBalloon;
};

class TalkStateHolder {
public:
    TalkStateHolder();

    void update();
    void pauseOff();
    TalkState* getState(const TalkMessageCtrl*);

    TalkSupportPlayerWatcher* mPlayerWatcher;
    IconAButton* mAButton;
    TalkStateShort* mTalkShort;
    TalkStateNormal* mTalkNormal;
    TalkStateEvent* mTalkEvent;
    TalkStateCompose* mTalkCompose;
    TalkState* mTalk;
    TalkBalloonShort* mBalloonShort;
};

namespace MR {
    void startTalkSound(u8, const LiveActor*);
};  // namespace MR
