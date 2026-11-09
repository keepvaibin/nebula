#include "Game/System/GalaxyCometScheduler.hpp"
#include "Game/System/NebulaDataTables.hpp"
#include "Game/System/GalaxyCometState.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/StringUtil.hpp"

const GalaxyCometTimePaper sCometTimeTableGrandGalaxy1[] = {
    {"EggStarGalaxy", GalaxyCometCategory_Normal},
    {"HoneyBeeKingdomGalaxy", GalaxyCometCategory_Normal},
    {"HoneyBeeKingdomGalaxy", GalaxyCometCategory_Coin100},
    {"EggStarGalaxy", GalaxyCometCategory_Coin100},
    {"EggStarGalaxy", GalaxyCometCategory_Normal},
    {"HoneyBeeKingdomGalaxy", GalaxyCometCategory_Normal},
    {"EggStarGalaxy", GalaxyCometCategory_Coin100},
    {"HoneyBeeKingdomGalaxy", GalaxyCometCategory_Coin100},
    {"EggStarGalaxy", GalaxyCometCategory_Normal},
    {"HoneyBeeKingdomGalaxy", GalaxyCometCategory_Coin100},
    {"EggStarGalaxy", GalaxyCometCategory_Coin100},
    {"HoneyBeeKingdomGalaxy", GalaxyCometCategory_Normal},
    {"HoneyBeeKingdomGalaxy", GalaxyCometCategory_Normal},
    {"EggStarGalaxy", GalaxyCometCategory_Normal},
    {"HoneyBeeKingdomGalaxy", GalaxyCometCategory_Coin100},
    {"EggStarGalaxy", GalaxyCometCategory_Coin100},
    {nullptr, 0},
};

const GalaxyCometTimePaper sCometTimeTableGrandGalaxy2[] = {
    {"StarDustGalaxy", GalaxyCometCategory_Normal},
    {"StarDustGalaxy", GalaxyCometCategory_Coin100},
    {"BattleShipGalaxy", GalaxyCometCategory_Normal},
    {"BattleShipGalaxy", GalaxyCometCategory_Coin100},
    {"StarDustGalaxy", GalaxyCometCategory_Coin100},
    {"BattleShipGalaxy", GalaxyCometCategory_Normal},
    {"StarDustGalaxy", GalaxyCometCategory_Normal},
    {"BattleShipGalaxy", GalaxyCometCategory_Coin100},
    {"StarDustGalaxy", GalaxyCometCategory_Normal},
    {"StarDustGalaxy", GalaxyCometCategory_Coin100},
    {"BattleShipGalaxy", GalaxyCometCategory_Normal},
    {"BattleShipGalaxy", GalaxyCometCategory_Coin100},
    {"StarDustGalaxy", GalaxyCometCategory_Normal},
    {"BattleShipGalaxy", GalaxyCometCategory_Normal},
    {"StarDustGalaxy", GalaxyCometCategory_Coin100},
    {"BattleShipGalaxy", GalaxyCometCategory_Coin100},
    {nullptr, 0},
};

const GalaxyCometTimePaper sCometTimeTableGrandGalaxy3[] = {
    {"PhantomGalaxy", GalaxyCometCategory_Normal},
    {"HeavenlyBeachGalaxy", GalaxyCometCategory_Normal},
    {"PhantomGalaxy", GalaxyCometCategory_Coin100},
    {"HeavenlyBeachGalaxy", GalaxyCometCategory_Coin100},
    {"HeavenlyBeachGalaxy", GalaxyCometCategory_Normal},
    {"PhantomGalaxy", GalaxyCometCategory_Coin100},
    {"HeavenlyBeachGalaxy", GalaxyCometCategory_Coin100},
    {"PhantomGalaxy", GalaxyCometCategory_Normal},
    {"HeavenlyBeachGalaxy", GalaxyCometCategory_Coin100},
    {"PhantomGalaxy", GalaxyCometCategory_Normal},
    {"HeavenlyBeachGalaxy", GalaxyCometCategory_Normal},
    {"PhantomGalaxy", GalaxyCometCategory_Coin100},
    {"PhantomGalaxy", GalaxyCometCategory_Normal},
    {"HeavenlyBeachGalaxy", GalaxyCometCategory_Coin100},
    {"PhantomGalaxy", GalaxyCometCategory_Coin100},
    {"HeavenlyBeachGalaxy", GalaxyCometCategory_Normal},
    {nullptr, 0},
};

const GalaxyCometTimePaper sCometTimeTableGrandGalaxy4[] = {
    {"SandClockGalaxy", GalaxyCometCategory_Normal},
    {"CosmosGardenGalaxy", GalaxyCometCategory_Normal},
    {"IceVolcanoGalaxy", GalaxyCometCategory_Normal},
    {"CosmosGardenGalaxy", GalaxyCometCategory_Coin100},
    {"SandClockGalaxy", GalaxyCometCategory_Normal},
    {"CosmosGardenGalaxy", GalaxyCometCategory_Normal},
    {"SandClockGalaxy", GalaxyCometCategory_Coin100},
    {"CosmosGardenGalaxy", GalaxyCometCategory_Coin100},
    {"SandClockGalaxy", GalaxyCometCategory_Normal},
    {"IceVolcanoGalaxy", GalaxyCometCategory_Coin100},
    {"SandClockGalaxy", GalaxyCometCategory_Coin100},
    {"CosmosGardenGalaxy", GalaxyCometCategory_Normal},
    {"IceVolcanoGalaxy", GalaxyCometCategory_Normal},
    {"CosmosGardenGalaxy", GalaxyCometCategory_Coin100},
    {"IceVolcanoGalaxy", GalaxyCometCategory_Coin100},
    {"SandClockGalaxy", GalaxyCometCategory_Normal},
    {"IceVolcanoGalaxy", GalaxyCometCategory_Normal},
    {"CosmosGardenGalaxy", GalaxyCometCategory_Coin100},
    {"SandClockGalaxy", GalaxyCometCategory_Coin100},
    {"IceVolcanoGalaxy", GalaxyCometCategory_Coin100},
    {"CosmosGardenGalaxy", GalaxyCometCategory_Normal},
    {"IceVolcanoGalaxy", GalaxyCometCategory_Normal},
    {"SandClockGalaxy", GalaxyCometCategory_Coin100},
    {"IceVolcanoGalaxy", GalaxyCometCategory_Coin100},
    {nullptr, 0},
};

const GalaxyCometTimePaper sCometTimeTableGrandGalaxy5[] = {
    {"OceanRingGalaxy", GalaxyCometCategory_Coin100},
    {"ReverseKingdomGalaxy", GalaxyCometCategory_Normal},
    {"OceanRingGalaxy", GalaxyCometCategory_Normal},
    {"ReverseKingdomGalaxy", GalaxyCometCategory_Coin100},
    {"FactoryGalaxy", GalaxyCometCategory_Normal},
    {"OceanRingGalaxy", GalaxyCometCategory_Coin100},
    {"FactoryGalaxy", GalaxyCometCategory_Normal},
    {"ReverseKingdomGalaxy", GalaxyCometCategory_Normal},
    {"FactoryGalaxy", GalaxyCometCategory_Coin100},
    {"OceanRingGalaxy", GalaxyCometCategory_Normal},
    {"ReverseKingdomGalaxy", GalaxyCometCategory_Normal},
    {"FactoryGalaxy", GalaxyCometCategory_Coin100},
    {"OceanRingGalaxy", GalaxyCometCategory_Normal},
    {"ReverseKingdomGalaxy", GalaxyCometCategory_Coin100},
    {"FactoryGalaxy", GalaxyCometCategory_Normal},
    {"ReverseKingdomGalaxy", GalaxyCometCategory_Normal},
    {"FactoryGalaxy", GalaxyCometCategory_Coin100},
    {"OceanRingGalaxy", GalaxyCometCategory_Coin100},
    {"FactoryGalaxy", GalaxyCometCategory_Normal},
    {"ReverseKingdomGalaxy", GalaxyCometCategory_Coin100},
    {"OceanRingGalaxy", GalaxyCometCategory_Normal},
    {"FactoryGalaxy", GalaxyCometCategory_Coin100},
    {"OceanRingGalaxy", GalaxyCometCategory_Coin100},
    {"ReverseKingdomGalaxy", GalaxyCometCategory_Coin100},
    {nullptr, 0},
};

const GalaxyCometTimePaper sCometTimeTableGrandGalaxy6[] = {
    {"CannonFleetGalaxy", GalaxyCometCategory_Coin100},
    {"OceanPhantomCaveGalaxy", GalaxyCometCategory_Coin100},
    {"CannonFleetGalaxy", GalaxyCometCategory_Normal},
    {"HellProminenceGalaxy", GalaxyCometCategory_Normal},
    {"CannonFleetGalaxy", GalaxyCometCategory_Coin100},
    {"HellProminenceGalaxy", GalaxyCometCategory_Coin100},
    {"OceanPhantomCaveGalaxy", GalaxyCometCategory_Normal},
    {"CannonFleetGalaxy", GalaxyCometCategory_Coin100},
    {"OceanPhantomCaveGalaxy", GalaxyCometCategory_Coin100},
    {"CannonFleetGalaxy", GalaxyCometCategory_Coin100},
    {"HellProminenceGalaxy", GalaxyCometCategory_Normal},
    {"OceanPhantomCaveGalaxy", GalaxyCometCategory_Coin100},
    {"CannonFleetGalaxy", GalaxyCometCategory_Normal},
    {"HellProminenceGalaxy", GalaxyCometCategory_Coin100},
    {"OceanPhantomCaveGalaxy", GalaxyCometCategory_Normal},
    {"HellProminenceGalaxy", GalaxyCometCategory_Normal},
    {"CannonFleetGalaxy", GalaxyCometCategory_Normal},
    {"OceanPhantomCaveGalaxy", GalaxyCometCategory_Normal},
    {"HellProminenceGalaxy", GalaxyCometCategory_Coin100},
    {"CannonFleetGalaxy", GalaxyCometCategory_Normal},
    {"HellProminenceGalaxy", GalaxyCometCategory_Normal},
    {"OceanPhantomCaveGalaxy", GalaxyCometCategory_Coin100},
    {"HellProminenceGalaxy", GalaxyCometCategory_Coin100},
    {"OceanPhantomCaveGalaxy", GalaxyCometCategory_Normal},
    {nullptr, 0},
};

const GalaxyCometTimePaper* const sGalaxyCometTimerPaper[] = {
    sCometTimeTableGrandGalaxy1, sCometTimeTableGrandGalaxy2, sCometTimeTableGrandGalaxy3,
    sCometTimeTableGrandGalaxy4, sCometTimeTableGrandGalaxy5, sCometTimeTableGrandGalaxy6,
};

// A mod's schedule for a dome, in place of the game's; null: the game's. A mod can have more domes than the game's six.
static MR::GrowBuffer< const GalaxyCometTimePaper* > sTimePaperSet;
static s32 sTimePaperCount;  // one past the highest dome a mod set

const GalaxyCometSerializeInfo cSerializeInfoPurpleCometAppear = {0x0007, 0};

const GalaxyCometSerializeInfo cSerializeInfoFirstContact[] = {
    {0x0001, 0}, {0x0000, 0}, {0x0000, 0}, {0x0000, 0}, {0x0000, 0}, {0x0000, 0},
};

GalaxyCometTimeTable::GalaxyCometTimeTable(const GalaxyCometTimePaper* pTimePaper)
    : mState(nullptr), mTimePaper(nullptr), mTimePaperPos(0), mIsReady(false), mIsHide(false) {
    mState = new GalaxyCometState();
    mTimePaper = pTimePaper;
    mTimePaperPos = 0;
}

void GalaxyCometTimeTable::update() {
    if (mIsReady) {
        mState->update();
        updateTimePaperPos();
    }
}

void GalaxyCometTimeTable::checkIsReady() {
    s32 pos;
    bool foundSuitableTimePaperPos = findSuitableTimePaperPos(&pos, -1);

    if (mIsReady != foundSuitableTimePaperPos) {
        mIsReady = foundSuitableTimePaperPos;
        if (foundSuitableTimePaperPos) {
            mTimePaperPos = pos;
        }
    }
}

void GalaxyCometTimeTable::serializeStateToGameData(GalaxyCometSerializeInfo* pInfo) {
    pInfo->mStateIndexPacked = (mTimePaperPos << 1) + mState->getStateIndex();
    pInfo->mPastSecond = mState->getPastSecond();
}

void GalaxyCometTimeTable::deserializeStateFromGameData(const GalaxyCometSerializeInfo* pInfo) {
    const u32 packed = pInfo->mStateIndexPacked;
    // A saved place past the end of the schedule (a save from another schedule) starts it over.
    const s32 pos = static_cast< s32 >(packed >> 1);
    mTimePaperPos = pos < calcTimePaperLength() ? pos : 0;
    mState->setStateAndPastSecond(packed & 1, pInfo->mPastSecond);
}

bool GalaxyCometTimeTable::isLand() const {
    if (!mIsReady) {
        return false;
    }
    return mState->isLand();
}

s32 GalaxyCometTimeTable::getCometCategory() const {
    return mTimePaper[mTimePaperPos].mCategory;
}

void GalaxyCometTimeTable::updateTimePaperPos() {
    if (!mIsHide) {
        if (mState->isHide()) {
            mIsReady = findSuitableTimePaperPos(&mTimePaperPos, mTimePaperPos);
        }
    }
    mIsHide = mState->isHide();
}

bool GalaxyCometTimeTable::findSuitableTimePaperPos(s32* pResultPos, s32 startPos) const {
    s32 timePaperPos = startPos;
    s32 length = calcTimePaperLength();
    for (s32 i = 0; i < length; i++) {
        timePaperPos++;
        if (mTimePaper[timePaperPos].mGalaxyName == nullptr) {
            timePaperPos = 0;
        }

        s32 category = mTimePaper[timePaperPos].mCategory;
        // A mod's schedule may name a galaxy the save keeps no stars for: it never appears.
        const char* galaxyName = GameDataFunction::findStarGalaxyName(mTimePaper[timePaperPos].mGalaxyName);
        bool canAppear;
        if (galaxyName == nullptr) {
            category = -1;
        }
        switch (category) {
        case GalaxyCometCategory_Normal:
            canAppear = MR::canAppearNormalComet(galaxyName);
            break;
        case GalaxyCometCategory_Coin100:
            canAppear = MR::canAppearCoin100Comet(galaxyName);
            break;
        default:
            canAppear = false;
        }

        if (canAppear) {
            *pResultPos = timePaperPos;
            return true;
        }
    }

    *pResultPos = timePaperPos;
    return false;
}

s32 GalaxyCometTimeTable::calcTimePaperLength() const {
    s32 length = 0;
    for (; mTimePaper[length].mGalaxyName != nullptr; length++)
        ;
    return length;
}

void GalaxyCometTimeTable::advance() {
    mState->forceToNext();
    updateTimePaperPos();
}

void GalaxyCometScheduler::setTimePaper(s32 dome, const GalaxyCometTimePaper* pPaper) {
    if (dome < 0) {
        return;
    }
    if (dome >= sTimePaperCount) {
        sTimePaperSet.room(dome + 1);
        for (s32 i = sTimePaperCount; i < dome; i++) {
            sTimePaperSet[i] = nullptr;
        }
        sTimePaperCount = dome + 1;
    }
    sTimePaperSet[dome] = pPaper;
}

GalaxyCometScheduler::GalaxyCometScheduler() : mTimeTables(), mEnabled(true) {
    // The game's six and any more a mod gave a schedule, one after another.
    s32 domes = 6;
    while (domes < sTimePaperCount && sTimePaperSet[domes] != nullptr) {
        domes++;
    }
    mTimeTables.init(domes);
    for (s32 i = 0; i < mTimeTables.size(); i++) {
        const GalaxyCometTimePaper* const pModPaper = i < sTimePaperCount ? sTimePaperSet[i] : nullptr;
        mTimeTables[i] = new GalaxyCometTimeTable(pModPaper != nullptr ? pModPaper : sGalaxyCometTimerPaper[i]);
    }
}

void GalaxyCometScheduler::update() {
    if (mEnabled) {
        for (s32 i = 0; i < mTimeTables.size(); i++) {
            mTimeTables[i]->update();
        }
    }
}

void GalaxyCometScheduler::activate() {
    mEnabled = true;
}

void GalaxyCometScheduler::deactivate() {
    mEnabled = false;
}

void GalaxyCometScheduler::forceToNextState() {
    for (s32 i = 0; i < mTimeTables.size(); i++) {
        mTimeTables[i]->advance();
    }

    if (GameDataFunction::isPowerStarLeftInCometOnly()) {
        setScheduleMostForwardCometAppear();
    }
}

void GalaxyCometScheduler::syncWithFlags() {
    for (s32 i = 0; i < mTimeTables.size(); i++) {
        mTimeTables[i]->checkIsReady();
    }

    if (GameDataFunction::isOnJustGameEventFlag("EventCometStarter")) {
        setScheduleFirstContact();
    }
}

void GalaxyCometScheduler::updateStateToGameData() {
    GalaxyCometSerializeInfo info;
    for (s32 i = 0; i < mTimeTables.size(); i++) {
        mTimeTables[i]->serializeStateToGameData(&info);
        GameDataFunction::updateGalaxyCometStatus(i + 1, info.mStateIndexPacked, info.mPastSecond);
    }
}

void GalaxyCometScheduler::restoreStateFromGameData() {
    GalaxyCometSerializeInfo info;
    for (s32 i = 0; i < mTimeTables.size(); i++) {
        GameDataFunction::restoreGalaxyCometStatus(i + 1, &info.mStateIndexPacked, &info.mPastSecond);
        mTimeTables[i]->deserializeStateFromGameData(&info);
    }

    if (GameDataFunction::isOnGameEventFlag("Coin100CometEggStarGalaxy")) {
        if (!GameDataFunction::isOnGameEventFlag("SpecialStarCoin100CometRelease")) {
            mTimeTables[0]->deserializeStateFromGameData(&cSerializeInfoPurpleCometAppear);
        }
    }
}

void GalaxyCometScheduler::setScheduleFirstContact() {
    // The game's six domes have a first-contact state; a mod's further domes start at the top of their schedule.
    for (s32 i = 0; i < mTimeTables.size(); i++) {
        mTimeTables[i]->deserializeStateFromGameData(&cSerializeInfoFirstContact[i < ARRAY_SIZE(cSerializeInfoFirstContact) ? i : 1]);
    }
}

void GalaxyCometScheduler::setScheduleMostForwardCometAppear() {
    if (isHideAll()) {
        for (s32 i = 0; i < mTimeTables.size(); i++) {
            GalaxyCometTimeTable* timeTable = mTimeTables[i];
            if (timeTable->mIsReady) {
                if (timeTable->isLand()) {
                    break;
                }
                timeTable->mState->forceToNext();
                timeTable->updateTimePaperPos();
                break;
            }
        }
    }
}

bool GalaxyCometScheduler::isCometLand(const char* pGalaxyName) const {
    for (s32 i = 0; i < mTimeTables.size(); i++) {
        GalaxyCometTimeTable* timeTable = mTimeTables[i];
        if (timeTable->isLand()) {
            const char* name = timeTable->mTimePaper[timeTable->mTimePaperPos].mGalaxyName;
            if (name != nullptr && MR::isEqualString(name, pGalaxyName)) {
                return true;
            }
        }
    }
    return false;
}

s32 GalaxyCometScheduler::getEncounterCometPowerStarId(const char* pGalaxyName) const {
    MR::makeGalaxyStatusAccessor(pGalaxyName);
    GalaxyCometTimeTable* timeTable = findFromGalaxy(pGalaxyName);

    if (timeTable == nullptr) {
        return 0;
    }

    switch (timeTable->getCometCategory()) {
    case GalaxyCometCategory_Normal:
        return 4;
    case GalaxyCometCategory_Coin100:
        return 5;
    default:
        return 0;
    }
}

const char* GalaxyCometScheduler::getEncounterCometName(const char* pGalaxyName) const {
    GalaxyStatusAccessor statusAccessor = MR::makeGalaxyStatusAccessor(pGalaxyName);
    s32 id = getEncounterCometPowerStarId(pGalaxyName);
    return statusAccessor.getCometName(id);
}

GalaxyCometTimeTable* GalaxyCometScheduler::findFromGalaxy(const char* pGalaxyName) const {
    for (s32 i = 0; i < mTimeTables.size(); i++) {
        GalaxyCometTimeTable* timeTable = mTimeTables[i];
        s32 pos = timeTable->mTimePaperPos;
        const char* name = timeTable->mTimePaper[pos].mGalaxyName;

        if (name != nullptr && MR::isEqualString(name, pGalaxyName)) {
            return timeTable;
        }
    }
    return nullptr;
}

bool GalaxyCometScheduler::isHideAll() const {
    for (s32 i = 0; i < mTimeTables.size(); i++) {
        GalaxyCometTimeTable* timeTable = mTimeTables[i];
        if (timeTable->isLand()) {
            return false;
        }
    }
    return true;
}

