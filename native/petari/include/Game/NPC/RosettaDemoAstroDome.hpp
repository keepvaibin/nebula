#pragma once

#include "Game/NPC/TalkTextFormer.hpp"
#include "Game/Screen/SimpleLayout.hpp"
#include "Game/System/NerveExecutor.hpp"

class IconAButton;
class NameObjArchiveListCollector;
class Rosetta;
class RosettaMonologue;

class RosettaDemoAstroDomeExplain : public NerveExecutor {
public:
    /// @brief Creates a new `RosettaDemoAstroDomeExplain`.
    /// @param pRosetta The pointer to the actor instance for Rosalina.
    /// @param rIter TODO.
    RosettaDemoAstroDomeExplain(Rosetta* pRosetta, const JMapInfoIter& rIter);

    static void makeArchiveList(NameObjArchiveListCollector* pCollector, const JMapInfoIter& rIter);

    void startDemo();
    void exeDemo();

    /// @brief The pointer to the actor instance for Rosalina.
    Rosetta* mRosetta;
    RosettaMonologue* mMonologue;
};

class RosettaDemoAstroDomeFinalBattle : public NerveExecutor {
public:
    /// @brief Creates a new `RosettaDemoAstroDomeFinalBattle`.
    /// @param pRosetta The pointer to the actor instance for Rosalina.
    /// @param rIter TODO.
    RosettaDemoAstroDomeFinalBattle(Rosetta* pRosetta, const JMapInfoIter& rIter);

    void startDemo();
    void exeDemo();

    /// @brief The pointer to the actor instance for Rosalina.
    Rosetta* mRosetta;
};

class RosettaDemoAstroDomeTalk : public NerveExecutor {
public:
    /// @brief Creates a new `RosettaDemoAstroDomeTalk`.
    /// @param pRosetta The pointer to the actor instance for Rosalina.
    /// @param rIter TODO.
    RosettaDemoAstroDomeTalk(Rosetta* pRosetta, const JMapInfoIter& rIter);

    void startDemo();
    void exeDemo();

    /// @brief The pointer to the actor instance for Rosalina.
    Rosetta* mRosetta;
};

class RosettaMonologue : public SimpleLayout {
public:
    /// @brief Creates a new `RosettaMonologue`.
    RosettaMonologue();

    virtual void movement();
    virtual void calcAnim();
    virtual void appear();
    virtual void control();

    s32 mMessageNo;
    TalkTextFormer mTextFormer;
    IconAButton* mIconAButton;
};
