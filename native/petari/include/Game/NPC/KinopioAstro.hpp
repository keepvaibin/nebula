#pragma once

#include "Game/NPC/Kinopio.hpp"

class LayoutActor;
class LuigiLetter;
class PeachLetter;

class KinopioAstro : public Kinopio {
public:
    // The letter event numbers of KinopioAstro's placement.
    enum LetterEvent {
        LetterEvent_LuigiLetter,
        LetterEvent_PeachLetterNormal,
        LetterEvent_PeachLetter1Ups,
        LetterEvent_SendLetter = 4,
        LetterEvent_OnMsgLedPattern,
        LetterEvent_OffMsgLedPattern,
    };

    KinopioAstro(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;


    bool dispLetter(LayoutActor*) const;
    bool isDispLetterIcon() const;
    void createLetterIcon(const TableIter&);
    bool branchFunc(u32);
    bool eventFunc(u32);
    void startDemo();
    void endDemo();

    LuigiLetter* mLuigiLetter;
    PeachLetter* mPeachLetter;
    TalkMessageCtrl* mLetterIconTalkCtrl;
    bool mHasPeachLetter;
    bool mHasLuigiLetter;
    bool mIsPlayerLuigi;
};
