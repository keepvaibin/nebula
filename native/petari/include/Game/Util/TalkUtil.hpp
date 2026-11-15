#pragma once

#include "Game/LumaMarks.hpp"
#include "Game/Util/MessageUtil.hpp"
#include <nebula_geometry.hpp>

class TableIter;
class LiveActor;
class TalkMessageCtrl;
class TalkMessageFuncBase;

namespace MR {
    class ActorTalkParam {
    public:
        void setNoTurnAction(const char* pActionName) {
            mWaitAction = pActionName;
            mTalkAction = pActionName;
            mWaitTurnAction = 0;
            mTalkTurnAction = 0;
            mTurnWhenNear = 0;
            mTurnOnTalk = 0;
        }

        void setSingleAction(const char* pActionName) {
            mWaitAction = pActionName;
            mWaitTurnAction = pActionName;
            mTalkAction = pActionName;
            mTalkTurnAction = pActionName;
        }

        inline void setDefaultAction() {
            setMoveAction("Wait", "Turn");
            setTalkAction("Talk", "TalkTurn");
        }

        inline void setMoveAction(const char* pMoveName, const char* pMoveTurnName) {
            mWaitAction = pMoveName;
            mWaitTurnAction = pMoveTurnName;
        }

        inline void setTalkAction(const char* pTalkName, const char* pTalkTurnName) {
            mTalkAction = pTalkName;
            mTalkTurnAction = pTalkTurnName;
        }

        inline void setMoveTalkNoTurnAction(const char* pMoveName, const char* pTalkName) {
            mWaitAction = pMoveName;
            mTalkAction = pTalkName;
            mWaitTurnAction = nullptr;
            mTalkTurnAction = nullptr;
            mTurnWhenNear = false;
            mTurnOnTalk = false;
        }

        u8 mTurnWhenNear;
        u8 mTurnOnTalk;
        f32 mNearDistance;
        f32 mTurnSpeed;
        f32 mTiltSpeed;
        f32 mTiltMaxAngle;
        const char* mWaitAction;
        const char* mWaitTurnAction;
        const char* mTalkAction;
        const char* mTalkTurnAction;
    };

    void registerBranchFunc(TalkMessageCtrl*, const TalkMessageFuncBase&);
    void registerEventFunc(TalkMessageCtrl*, const TalkMessageFuncBase&);
    void registerAnimeFunc(TalkMessageCtrl*, const TalkMessageFuncBase&);
    void registerKillFunc(TalkMessageCtrl*, const TalkMessageFuncBase&);
    void setMessageArg(TalkMessageCtrl*, int);
    void setMessageArg(TalkMessageCtrl*, const char16_t*);
    TalkMessageCtrl* createTalkCtrl(LiveActor*, const TableIter&, const char*, const Vec3f&, LUMA_KEPT LUMA_NULLABLE MtxPtr);
    TalkMessageCtrl* createTalkCtrlDirect(LiveActor*, const TableIter&, const char*, const Vec3f&, LUMA_KEPT LUMA_NULLABLE MtxPtr);
    TalkMessageCtrl* createTalkCtrlDirectOnRootNodeAutomatic(LiveActor*, const TableIter&, const char*, const Vec3f&, LUMA_KEPT LUMA_NULLABLE MtxPtr);
    bool tryTalkNearPlayer(TalkMessageCtrl*);
    bool tryTalkNearPlayerAtEnd(TalkMessageCtrl*);
    bool tryTalkForce(TalkMessageCtrl*);
    bool tryTalkForceAtEnd(TalkMessageCtrl*);
    bool tryTalkForceWithoutDemo(TalkMessageCtrl*);
    bool tryTalkForceWithoutDemoMarioPuppetable(TalkMessageCtrl*);
    bool tryTalkForceWithoutDemoAtEnd(TalkMessageCtrl*);
    bool tryTalkForceWithoutDemoMarioPuppetableAtEnd(TalkMessageCtrl*);
    bool tryTalkTimeKeepDemo(TalkMessageCtrl*);
    bool tryTalkTimeKeepDemoMarioPuppetable(TalkMessageCtrl*);
    bool tryTalkTimeKeepDemoWithoutPauseMarioPuppetable(TalkMessageCtrl*);
    bool tryTalkRequest(TalkMessageCtrl*);
    bool tryTalkSelectLeft(TalkMessageCtrl*);
    bool tryTalkSelectRight(TalkMessageCtrl*);
    MtxPtr getMessageBalloonFollowMatrix(const TalkMessageCtrl*);
    const Vec3f& getMessageBalloonFollowOffset(const TalkMessageCtrl*);
    void setMessageBalloonFollowOffset(TalkMessageCtrl*, const Vec3f&);
    bool isNearPlayer(const TalkMessageCtrl*, f32);
    bool inMessageArea(const TalkMessageCtrl*);
    bool isTalkNone(const TalkMessageCtrl*);
    bool isTalkEntry(const TalkMessageCtrl*);
    bool isTalkTalking(const TalkMessageCtrl*);
    bool isTalkEnableEnd(const TalkMessageCtrl*);
    void clearTalkState(TalkMessageCtrl*);
    void resetNode(TalkMessageCtrl*);
    void readMessage(TalkMessageCtrl*);
    void forwardNode(TalkMessageCtrl*);
    void resetAndForwardNode(TalkMessageCtrl*, s32);
    void forwardNodeNextBranchLeft(TalkMessageCtrl*);
    void forwardNodeNextBranchRight(TalkMessageCtrl*);
    void forwardNodeCurrentBranchLeft(TalkMessageCtrl*);
    void forwardNodeCurrentBranchRight(TalkMessageCtrl*);
    bool tryForwardNode(TalkMessageCtrl*);
    bool isExistNextNode(const TalkMessageCtrl*);
    bool isShortTalk(const TalkMessageCtrl*);
    void setDistanceToTalk(TalkMessageCtrl*, f32);
    void onRootNodeAutomatic(TalkMessageCtrl*);
    void offRootNodeAutomatic(TalkMessageCtrl*);
    void onReadNodeAutomatic(TalkMessageCtrl*);
    void offReadNodeAutomatic(TalkMessageCtrl*);
    void onStartOnlyFront(TalkMessageCtrl*);
    bool isTalkStart(const TalkMessageCtrl*);
    bool isTalkEnd(const TalkMessageCtrl*);

    inline void setMessageArgToCurrentGalaxyName(TalkMessageCtrl* pCtrl, const char* pName) {
        MR::setMessageArg(pCtrl, MR::getGalaxyNameOnCurrentLanguage(pName));
    }
};  // namespace MR
