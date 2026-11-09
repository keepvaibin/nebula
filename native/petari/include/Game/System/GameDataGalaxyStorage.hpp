#pragma once

#include "Game/System/BinaryDataChunkHolder.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/NameIndex.hpp"

class BinaryDataContentAccessor;
class BinaryDataContentHeaderSerializer;
class GalaxyStatusAccessor;

class GameDataSomeGalaxyStorage {
public:
    GameDataSomeGalaxyStorage(const GalaxyStatusAccessor&);
    // No missions: what a galaxy the save keeps nothing for reads (nothing owned, nothing kept).
    GameDataSomeGalaxyStorage();

    void resetAllData();
    s32 getPowerStarNumOwned() const;
    s32 deserialize(const BinaryDataContentAccessor&, const u8*);
    void serialize(const BinaryDataContentAccessor&, u8*) const;

    // A mission index (scenario number - 1) it has room for; scenario 0 gives -1, which has none.
    bool hasSlot(int scenarioNo) const {
        return static_cast< u32 >(scenarioNo) < static_cast< u32 >(mSlots);
    }

    bool hasPowerStar(int scenarioNo) const {
        return hasSlot(scenarioNo) && (mPowerStarOwnedFlags[scenarioNo >> 3] & (1 << (scenarioNo & 7))) != 0;
    }

    bool isAlreadyVisited(int scenarioNo) const {
        return hasSlot(scenarioNo) && (mAlreadyVisitedFlags[scenarioNo >> 3] & (1 << (scenarioNo & 7))) != 0;
    }

    u16 getMaxCoinNum(int scenarioNo) const {
        return hasSlot(scenarioNo) ? mMaxCoinNum[scenarioNo] : 0;
    }

    void setMaxCoinNum(s32 scenarioNo, u16 coinNum) {
        if (hasSlot(scenarioNo)) {
            mMaxCoinNum[scenarioNo] = coinNum;
        }
    }

    const char* mGalaxyName;
    s32 mPowerStarNum;
    // Missions it has room for: eight as the game's save has them, or as many as its data says.
    s32 mSlots;
    u8* mPowerStarOwnedFlags;
    u8* mAlreadyVisitedFlags;
    u16* mMaxCoinNum;
};

class GameDataSomeScenarioAccessor {
public:
    GameDataSomeScenarioAccessor(GameDataSomeGalaxyStorage* pStorage, s32 scenarioNum) : mSomeGalaxyStorage(pStorage), mScenarioNum(scenarioNum) {
    }

    bool hasPowerStar() const;
    bool isAlreadyVisited() const;
    s32 getMaxCoinNum() const;
    void setPowerStarFlag(bool isOwned);
    void setFlagAlreadyVisited(bool isVisited);
    void updateMaxCoinNum(int coinNum);

    void setBitFlagAccordingToBool(u8* pFlags, bool isOn);

    GameDataSomeGalaxyStorage* mSomeGalaxyStorage;
    u32 mScenarioNum;
};

class GameDataAllGalaxyStorage : public BinaryDataChunkBase {
public:
    // Missions past eight in the galaxy with the most.
    s32 getExtraStars() const;
    GameDataAllGalaxyStorage();

    virtual u32 makeHeaderHashCode() const override;
    virtual u32 getSignature() const override;
    virtual s32 serialize(u8*, u32) const override;
    virtual s32 deserialize(const u8*, u32) override;
    virtual void initializeData() override;

    bool isExistAccessor(const char*, int) const;
    GameDataSomeScenarioAccessor makeAccessor(const char*, int) const;
    s32 calcCurrentPowerStarNum() const;
    s32 getPowerStarNumOwned(const char*) const;
    s32 getGalaxyNum() const;
    GameDataSomeGalaxyStorage* getGalaxyStorage(int);
    void initHeaderSerializer();
    s32 findIndex(const char*) const;
    s32 findIndex(u16) const;

    BinaryDataContentHeaderSerializer* getSerializer() const {
        return mHeaderSerializer;
    }

    MR::Vector< MR::AssignableArray< GameDataSomeGalaxyStorage* > > mSomeGalaxyStorages;
    BinaryDataContentHeaderSerializer* mHeaderSerializer;
    MR::NameIndex mNames;       // galaxy name -> index in mSomeGalaxyStorages
    MR::NameIndex mSaveHashes;  // keyed by the 16-bit name hash a save holds, first galaxy of a hash
    GameDataSomeGalaxyStorage* mNoGalaxy;  // makeAccessor's for a name it holds none for
};
