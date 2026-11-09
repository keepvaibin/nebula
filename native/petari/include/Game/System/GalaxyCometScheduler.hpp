#pragma once

#include "Game/Util/Array.hpp"

class GalaxyCometState;

// A schedule entry's comet (GalaxyCometTimePaper::mCategory; a mod's schedule gives the same values).
enum GalaxyCometCategory {
    GalaxyCometCategory_Normal = 0,
    GalaxyCometCategory_Coin100 = 1,
};

struct GalaxyCometSerializeInfo {
    // (schedule position << 1) | GalaxyCometState::State.
    u16 mStateIndexPacked;
    u16 mPastSecond;
};

struct GalaxyCometTimePaper {
    const char* mGalaxyName;
    // A GalaxyCometCategory.
    s32 mCategory;
};

// One dome's comet schedule: a null-terminated list of galaxies that take turns.
class GalaxyCometTimeTable {
public:
    GalaxyCometTimeTable(const GalaxyCometTimePaper*);

    void update();
    void checkIsReady();
    void serializeStateToGameData(GalaxyCometSerializeInfo*);
    void deserializeStateFromGameData(const GalaxyCometSerializeInfo*);
    bool isLand() const;
    s32 getCometCategory() const;
    // Moves to the next entry that can appear once the comet hides.
    void updateTimePaperPos();
    // The first entry after startPos (wrapping) whose comet can appear.
    bool findSuitableTimePaperPos(s32*, s32) const;
    s32 calcTimePaperLength() const;
    void advance();

    GalaxyCometState* mState;
    const GalaxyCometTimePaper* mTimePaper;
    s32 mTimePaperPos;
    // Some entry can appear.
    bool mIsReady;
    // The state was hidden at the last update.
    bool mIsHide;
};

class GalaxyCometScheduler {
public:
    // A mod's schedule for a dome, before the scheduler is made.
    static void setTimePaper(s32 dome, const GalaxyCometTimePaper* pPaper);

    GalaxyCometScheduler();

    void update();
    void activate();
    void deactivate();
    void forceToNextState();
    void syncWithFlags();
    void updateStateToGameData();
    // Egg Star Galaxy's 100-coin comet stays out until its star is released.
    void restoreStateFromGameData();
    void setScheduleFirstContact();
    // With every comet hidden, the first ready dome moves on.
    void setScheduleMostForwardCometAppear();
    bool isCometLand(const char*) const;
    // The comet's star id, or 0 for no comet.
    s32 getEncounterCometPowerStarId(const char*) const;
    const char* getEncounterCometName(const char*) const;
    // The dome whose current entry is the galaxy, or null.
    GalaxyCometTimeTable* findFromGalaxy(const char*) const;
    bool isHideAll() const;
    // The packed state of the dome whose schedule has the galaxy, or -1.

private:
    MR::AssignableArray< GalaxyCometTimeTable* > mTimeTables;
    bool mEnabled;
};
