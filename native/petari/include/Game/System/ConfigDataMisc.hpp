#pragma once

#include "Game/System/NebulaCalendar.hpp"
#include "Game/System/BinaryDataChunkHolder.hpp"

class ConfigDataMisc : public BinaryDataChunkBase {
public:
    ConfigDataMisc();

    virtual u32 makeHeaderHashCode() const override;
    virtual u32 getSignature() const override;
    virtual s32 serialize(u8*, u32) const override;
    virtual s32 deserialize(const u8*, u32) override;
    virtual void initializeData() override;

    bool isLastLoadedMario() const;
    void setLastLoadedMario(bool);
    void onCompleteEndingMario();
    void onCompleteEndingLuigi();
    bool isOnCompleteEndingMario();
    bool isOnCompleteEndingLuigi();
    NebulaCalendar::SaveTime getLastModified() const;
    void updateLastModified();

private:
    u8 mFlag;
    NebulaCalendar::SaveTime mLastModified;
};
