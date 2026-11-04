#pragma once

#include "Game/System/BinaryDataChunkHolder.hpp"
#include "Game/Util/Array.hpp"

class BinaryDataContentAccessor;
class BinaryDataContentHeaderSerializer;
class GalaxyStatusAccessor;

class GameDataSomeGalaxyStorage {
public:
    GameDataSomeGalaxyStorage(const GalaxyStatusAccessor&);

    void resetAllData();
    s32 getPowerStarNumOwned() const;
    s32 deserialize(const BinaryDataContentAccessor&, const u8*);
    void serialize(const BinaryDataContentAccessor&, u8*) const;

    bool hasPowerStar(int scenarioNo) const {
        return scenarioNo < mSlots && (mPowerStarOwnedFlags[scenarioNo >> 3] & (1 << (scenarioNo & 7))) != 0;
    }

    bool isAlreadyVisited(int scenarioNo) const {
        return scenarioNo < mSlots && (mAlreadyVisitedFlags[scenarioNo >> 3] & (1 << (scenarioNo & 7))) != 0;
    }

    u16 getMaxCoinNum(int scenarioNo) const {
        return scenarioNo < mSlots ? mMaxCoinNum[scenarioNo] : 0;
    }

    void setMaxCoinNum(s32 scenarioNo, u16 coinNum) {
        if (scenarioNo < mSlots) {
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
    void setPowerStarFlag(bool);
    void setFlagAlreadyVisited(bool);
    void updateMaxCoinNum(int);

    void setBitFlagAccordingToBool(u8*, bool);

    GameDataSomeGalaxyStorage* mSomeGalaxyStorage;
    u32 mScenarioNum;
};

// NOTE/TODO: This dummy is here to ensure vtable alignment in GameDataAllGalaxyStorage.
// This double-inheritance with BinaryDataChunkBase behavior is repeated for a few
// different classes. What actually is the class that takes this role?
class Dummy1 {
public:
    Dummy1() {};
};

class GameDataAllGalaxyStorage : public Dummy1, public BinaryDataChunkBase {
public:
    // Missions past eight in the galaxy with the most, and what they add to the save.
    s32 getExtraStars() const;
    u32 getExtraBytes() const;
    GameDataAllGalaxyStorage();

    virtual u32 makeHeaderHashCode() const;
    virtual u32 getSignature() const;
    virtual s32 serialize(u8*, u32) const;
    virtual s32 deserialize(const u8*, u32);
    virtual void initializeData();

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
};
