#pragma once

#include "Game/System/NerveExecutor.hpp"

class JMapInfoIter;
class Rosetta;

class RosettaDemoEpilogue : public NerveExecutor {
public:
    /// @brief Creates a new `RosettaDemoEpilogue`.
    /// @param pRosetta The pointer to the actor instance for Rosalina.
    /// @param rIter TODO.
    RosettaDemoEpilogue(Rosetta* pRosetta, const JMapInfoIter& rIter);

    void startDemo();
    void exeDemo();

private:
    /// @brief The pointer to the actor instance for Rosalina.
    Rosetta* mRosetta;

    /// @brief TODO.
    bool mIsFadeOut;
};
