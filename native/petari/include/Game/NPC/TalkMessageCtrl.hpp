#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;
class LiveActor;
class TalkMessageFuncBase;
class TalkMessageInfo;
class TalkNodeCtrl;

// TalkMessageCtrl::mTalkStateKind.
enum TalkStateKind {
    TalkState_None,
    TalkState_Entry,
    TalkState_EnableStart,
    TalkState_Talking,
    TalkState_EnableEnd,
};

class CustomTagArg {
public:
    enum TagType { Type_Int = 0, Type_Char = 1, Type_Uninitialized = 2 };

    inline CustomTagArg(int intArg, TagType type) : mIntArg(intArg), mArgType(type) {
    }
    inline CustomTagArg(const char16_t* pCharArg, TagType type) : mCharArg(pCharArg), mArgType(type) {
    }

    inline void operator=(const CustomTagArg& rhs) {
        mCharArg = rhs.mCharArg;
        mArgType = rhs.mArgType;
    }

    union {
        int mIntArg;
        const char16_t* mCharArg;
    };

    TagType mArgType;
};

class TalkMessageCtrl : public NameObj {
public:
    // pBalloonMtx, when set, places the balloon and the talk range instead of the host.
    TalkMessageCtrl(LiveActor* pHost, const Vec3f& rBalloonOffset, MtxPtr pBalloonMtx);

    void createMessage(const TableIter&, const char*);
    void createMessageDirect(const TableIter&, const char*);
    u32 getMessageID() const;
    bool requestTalk();
    bool requestTalkForce();
    bool startTalk();
    bool startTalkForce();
    bool startTalkForcePuppetable();
    bool startTalkForceWithoutDemo();
    bool startTalkForceWithoutDemoPuppetable();
    bool endTalk();
    void updateBalloonPos();
    // True when this host is nearer the player than pOther's (or pOther is null).
    bool isNearPlayer(const TalkMessageCtrl* pOther);
    // distance <= 0 uses mTalkDistance.
    bool isNearPlayer(f32 distance) const;
    void rootNodePre(bool isStopAtEvent);
    void rootNodePst();
    bool isCurrentNodeContinue() const;
    bool rootNodeEve();
    void rootNodeSel(bool isYes);
    void registerBranchFunc(const TalkMessageFuncBase&);
    void registerEventFunc(const TalkMessageFuncBase&);
    void registerAnimeFunc(const TalkMessageFuncBase&);
    void registerKillFunc(const TalkMessageFuncBase&);
    void readMessage();
    bool isSelectYesNo() const;

    void setMessageArg(const CustomTagArg& rArg) {
        mTagArg = rArg;
    }

    void setMessageBallonFollowOffs(const Vec3f& rVec) {
        mMsgBalloonFollowOffs = rVec;
    }

    bool inMessageArea() const;
    void startCamera(s32 frames);
    const char* getBranchID() const;

    LiveActor* mHostActor;
    TalkNodeCtrl* mNodeCtrl;
    s32 mZoneID;
    u32 mTalkStateKind;
    Vec3f mBalloonPos;
    MtxPtr mMtx;
    Vec3f mMsgBalloonFollowOffs;
    f32 mTalkDistance;
    u32 mAlreadyDoneFlag;
    u32 mAlreadyDoneFlags;
    bool mIsOnRootNodeAuto;
    bool mIsOnReadNodeAuto;
    bool mIsStartOnlyFront;
    ActorCameraInfo* mCameraInfo;
    TalkMessageFuncBase* mBranchFunc;
    TalkMessageFuncBase* mEventFunc;
    TalkMessageFuncBase* mAnimeFunc;
    TalkMessageFuncBase* mKillFunc;
    CustomTagArg mTagArg;
};

class TalkFunction {
public:
    static bool isShortTalk(const TalkMessageCtrl*);
    static bool isComposeTalk(const TalkMessageCtrl*);
    static bool isSelectTalk(const TalkMessageCtrl*);
    static bool isEventNode(const TalkMessageCtrl*);

    static bool requestTalkSystem(TalkMessageCtrl*, bool);
    static bool startTalkSystem(TalkMessageCtrl*, bool, bool, bool);
    static bool endTalkSystem(TalkMessageCtrl*);
    static bool isTalkSystemStart(const TalkMessageCtrl*);
    static bool isTalkSystemEnd(const TalkMessageCtrl*);
    static bool getBranchAstroGalaxyResult(u16);
    static void registerTalkSystem(TalkMessageCtrl*);

    static TalkMessageInfo* getMessageInfo(const TalkMessageCtrl*);
    static const char16_t* getSubMessage(const TalkMessageCtrl*);
    static const char16_t* getMessage(const TalkMessageCtrl*);
    static void onTalkStateEntry(TalkMessageCtrl*);
    static void onTalkStateNone(TalkMessageCtrl*);
    static void onTalkStateEnableStart(TalkMessageCtrl*);
    static void onTalkStateTalking(TalkMessageCtrl*);
    static void onTalkStateEnableEnd(TalkMessageCtrl*);
};
