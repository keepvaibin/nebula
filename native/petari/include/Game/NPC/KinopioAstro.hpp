#pragma once

#include "Game/NPC/Kinopio.hpp"

class LayoutActor;
class LuigiLetter;
class PeachLetter;

class KinopioAstro : public Kinopio {
public:
    enum LetterEvent {
        LetterEvent_LuigiLetter,
        LetterEvent_PeachLetterNormal,
        LetterEvent_PeachLetter1Ups,
        LetterEvent_3,
        LetterEvent_SendLetter,
        LetterEvent_OnMsgLedPattern,
        LetterEvent_OffMsgLedPattern,
    };

    /// @brief Creates a new `KinopioAstro`.
    /// @param pName A pointer to the null-terminated name of the object.
    KinopioAstro(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();

    static void makeArchiveList(NameObjArchiveListCollector*, const JMapInfoIter&);

    bool dispLetter(LayoutActor*) const;
    bool isDispLetterIcon() const;
    void createLetterIcon(const JMapInfoIter&);
    bool branchFunc(u32);
    bool eventFunc(u32);
    void startDemo();
    void endDemo();

    LuigiLetter* mLuigiLetter;
    PeachLetter* mPeachLetter;
    TalkMessageCtrl* mLetterIconTalkCtrl;
    bool _194;
    bool _195;
    bool mIsPlayerLuigi;
};
