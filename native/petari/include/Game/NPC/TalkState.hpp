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

    TalkMessageCtrl* _04;
    TalkBalloon* mBalloon;
    u32 mMessageID;
};

class TalkStateShort : public TalkState {
public:
    TalkStateShort();

    virtual bool prep(const TalkMessageCtrl*);
    virtual void open();
    virtual bool talk(const TalkMessageCtrl*);
    virtual void clos();
};

class TalkStateEvent : public TalkState {
public:
    TalkStateEvent();

    virtual bool prep(const TalkMessageCtrl*);
    virtual bool test();
    virtual void open();
    virtual bool talk(const TalkMessageCtrl*);
    virtual void clos();
    virtual bool term(const TalkMessageCtrl*);
    virtual u32 getPageCount() const;

    s32 _10;
    s32 _14;
    TalkSupportPlayerWatcher* _18;
    u8 _1C;
    bool _1D;
    u32 mPageCount;
};

class TalkStateNormal : public TalkStateEvent {
public:
    TalkStateNormal();

    virtual bool prep(const TalkMessageCtrl*);
    virtual bool test();
    virtual void clos();
    virtual bool term(const TalkMessageCtrl*);
    void updateButton();

    IconAButton* mAButton;
};

class TalkStateCompose : public TalkStateNormal {
public:
    TalkStateCompose();

    virtual void init(TalkMessageCtrl*, TalkBalloon*);
    virtual bool prep(const TalkMessageCtrl*);
    virtual bool test();
    virtual void open();

    TalkBalloon* mSecondBalloon;
};

class TalkStateHolder {
public:
    TalkStateHolder();

    void update();
    void pauseOff();
    TalkState* getState(const TalkMessageCtrl*);

    TalkSupportPlayerWatcher* _00;
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
