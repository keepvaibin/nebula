#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <JSystem/JGeometry/TVec.hpp>

class ActorCameraInfo;
class LiveActor;
class TalkMessageFuncBase;
class TalkMessageInfo;
class TalkNodeCtrl;

enum TalkMessageCtrl_Type {
    TalkMessageCtrl_Type0,
    TalkMessageCtrl_Type1,
    TalkMessageCtrl_Type2,
    TalkMessageCtrl_Type3,
    TalkMessageCtrl_Type4,
    TalkMessageCtrl_Type5,
    TalkMessageCtrl_Type6,
    TalkMessageCtrl_Type7,
    TalkMessageCtrl_Type8,
    TalkMessageCtrl_Type9,
    TalkMessageCtrl_TypeA,
    // After 0xA the branch types are the galaxy result (TypeE), the time-keep demo (TypeB), then a message read before
    // (TypeC), the order TalkMessageCtrl::rootNodePre branches on. Numbered B, C, E the nodes of those
    // types asked the wrong question, and a character's line after a galaxy took the wrong branch.
    TalkMessageCtrl_TypeE,
    TalkMessageCtrl_TypeB,
    TalkMessageCtrl_TypeC,
};

enum BranchNode_Type {
    BranchNode_Type0,
    BranchNode_Type1,
    BranchNode_Type2,
    BranchNode_Type3,
    BranchNode_Type4,
    BranchNode_Type5,
    BranchNode_Type6,
    BranchNode_Type7,
    BranchNode_Type8,
    BranchNode_Type9,
    BranchNode_TypeA,
    BranchNode_TypeB,
    BranchNode_TypeC,
    BranchNode_TypeD,
    BranchNode_TypeE,
    BranchNode_TypeF,
    BranchNode_Type10,
    BranchNode_Type11,
    BranchNode_Type12,
};

class CustomTagArg {
public:
    enum TagType { Type_Int = 0, Type_Char = 1, Type_Uninitialized = 2 };

    inline CustomTagArg(int a1, TagType a2) : mIntArg(a1), mArgType(a2) {
    }
    inline CustomTagArg(const char16_t* a1, TagType a2) : mCharArg(a1), mArgType(a2) {
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
    TalkMessageCtrl(LiveActor*, const TVec3f&, MtxPtr);

    virtual ~TalkMessageCtrl();

    void createMessage(const JMapInfoIter&, const char*);
    void createMessageDirect(const JMapInfoIter&, const char*);
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
    bool isNearPlayer(const TalkMessageCtrl*);
    bool isNearPlayer(f32) const;
    void rootNodePre(bool);
    void rootNodePst();
    bool isCurrentNodeContinue() const;
    bool rootNodeEve();
    void rootNodeSel(bool);
    void registerBranchFunc(const TalkMessageFuncBase&);
    void registerEventFunc(const TalkMessageFuncBase&);
    void registerAnimeFunc(const TalkMessageFuncBase&);
    void registerKillFunc(const TalkMessageFuncBase&);
    void readMessage();
    bool isSelectYesNo() const;

    void setMessageArg(const CustomTagArg& rArg) {
        mTagArg = rArg;
    }

    void setMessageBallonFollowOffs(const TVec3f& rVec) {
        mMsgBalloonFollowOffs = rVec;
    }

    bool inMessageArea() const;
    void startCamera(s32);
    const char* getBranchID() const;

    LiveActor* mHostActor;
    TalkNodeCtrl* mNodeCtrl;
    s32 mZoneID;
    u32 _18;
    TVec3f _1C;
    MtxPtr mMtx;
    TVec3f mMsgBalloonFollowOffs;
    f32 mTalkDistance;
    u32 _3C;
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
