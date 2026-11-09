#pragma once

#include "Game/System/BinaryDataChunkHolder.hpp"
#include "Game/Util/Array.hpp"

class GalaxyStatusAccessor;

// How far one spin driver's path has been drawn (0 to 1; -1 unused).
class SpinDriverPathStorageOne {
public:
    SpinDriverPathStorageOne();

    void reset();
    f32 registerToUse(int zoneId, int spinDriverId);
    void updateValue(f32 drawRange);
    s32 serialize(u8* pData) const;
    s32 deserialize(const u8* pData, int zoneId);

    f32 getDrawRange() const {
        return mDrawRange;
    }

    f32 getDrawRangeScale() const {
        return (1.0f / 256.0f);
    }

    s32 mZoneId;
    s32 mSpinDriverId;
    f32 mDrawRange;
};

class SpinDriverPathStorageScenario {
public:
    SpinDriverPathStorageScenario();

    void resetAllData();
    s32 setup(int zoneId, int spinDriverId, f32* pDrawRange);
    void updateValue(int spinDriverIndex, f32 drawRange);
    s32 serialize(u8* pData, u32 dataSize) const;
    s32 deserialize(const u8* pData, u32 dataSize);
    s32 calcMaxZoneId() const;

    MR::Vector< MR::AssignableArray< SpinDriverPathStorageOne > > mOneStorage;
};

class SpinDriverPathStorageGalaxy {
public:
    SpinDriverPathStorageGalaxy(const GalaxyStatusAccessor& rAccessor);

    void resetAllData();
    void updateValue(int scenarioNo, int spinDriverIndex, f32 drawRange);
    s32 serialize(u8* pData, u32 maxBufferSize) const;
    s32 deserialize(const u8* pData, u32 maxBufferSize);

    SpinDriverPathStorageScenario& getScenarioStorage(int scenarioNo) {
        return mScenarioStorage[scenarioNo - 1];
    }

    SpinDriverPathStorageScenario* mScenarioStorage;
    s32 mNumScenarios;
    const char* mGalaxyName;
};

class SpinDriverPathStorage : public BinaryDataChunkBase {
public:
    SpinDriverPathStorage();

    virtual u32 makeHeaderHashCode() const override;
    virtual u32 getSignature() const override;
    virtual s32 serialize(u8* pData, u32 maxBufferSize) const override;
    virtual s32 deserialize(const u8* pData, u32 maxBufferSize) override;
    virtual void initializeData() override;

    s32 setup(const char* pGalaxyName, int scenarioNo, int zoneId, int spinDriverId, f32* pDrawRange);
    void updateValue(const char* pGalaxyName, int scenarioNo, int spinDriverIndex, f32 drawRange);
    SpinDriverPathStorageGalaxy* findFromGalaxy(const char* pGalaxyName);
    SpinDriverPathStorageGalaxy* findFromHashCode(u16 galaxyNameHash);

    MR::Vector< MR::AssignableArray< SpinDriverPathStorageGalaxy* > > mGalaxyStorage;
};
