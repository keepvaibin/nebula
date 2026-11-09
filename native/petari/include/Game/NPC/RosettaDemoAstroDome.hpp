#pragma once

#include "Game/NPC/TalkTextFormer.hpp"
#include "Game/Screen/SimpleLayout.hpp"
#include "Game/System/NerveExecutor.hpp"

class IconAButton;
class Rosetta;
class RosettaMonologue;

class RosettaDemoAstroDomeExplain : public NerveExecutor {
public:
    /// @param pRosetta The pointer to the actor instance for Rosalina.
    /// @param rIter Rosalina's placement entry.
    RosettaDemoAstroDomeExplain(Rosetta* pRosetta, const TableIter& rIter);


    void startDemo();
    void exeDemo();

    /// @brief The pointer to the actor instance for Rosalina.
    Rosetta* mRosetta;
    RosettaMonologue* mMonologue;
};

class RosettaDemoAstroDomeFinalBattle : public NerveExecutor {
public:
    /// @param pRosetta The pointer to the actor instance for Rosalina.
    /// @param rIter Rosalina's placement entry.
    RosettaDemoAstroDomeFinalBattle(Rosetta* pRosetta, const TableIter& rIter);

    void startDemo();
    void exeDemo();

    /// @brief The pointer to the actor instance for Rosalina.
    Rosetta* mRosetta;
};

class RosettaDemoAstroDomeTalk : public NerveExecutor {
public:
    /// @param pRosetta The pointer to the actor instance for Rosalina.
    /// @param rIter Rosalina's placement entry.
    RosettaDemoAstroDomeTalk(Rosetta* pRosetta, const TableIter& rIter);

    void startDemo();
    void exeDemo();

    /// @brief The pointer to the actor instance for Rosalina.
    Rosetta* mRosetta;
};

class RosettaMonologue : public SimpleLayout {
public:
    RosettaMonologue();

    virtual void movement() override;
    virtual void calcAnim() override;
    virtual void appear() override;
    virtual void control() override;

    s32 mMessageNo;
    TalkTextFormer mTextFormer;
    IconAButton* mIconAButton;
};
