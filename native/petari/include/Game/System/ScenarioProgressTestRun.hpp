#pragma once

class GameDataHolder;

class ScenarioProgressTestRun {
public:
    /// @param pHost The pointer to the owning container for persistent gameplay data.
    ScenarioProgressTestRun(GameDataHolder* pHost);

private:
    /// @brief The pointer to the owning container for persistent gameplay data.
    GameDataHolder* mHost;
};
