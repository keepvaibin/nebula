#pragma once

#include "Game/System/NerveExecutor.hpp"

class TableIter;
class Rosetta;

class RosettaDemoEpilogue : public NerveExecutor {
public:
    /// @param pRosetta The pointer to the actor instance for Rosalina.
    /// @param rIter Rosalina's placement entry.
    RosettaDemoEpilogue(Rosetta* pRosetta, const TableIter& rIter);

    void startDemo();
    void exeDemo();

private:
    /// @brief The pointer to the actor instance for Rosalina.
    Rosetta* mRosetta;

    /// @brief Set once the fade-out demo part starts; the baby cry stops from then on.
    bool mIsFadeOut;
};
