#pragma once

#include "Game/System/BinaryDataChunkHolder.hpp"
#include "Game/Mii/Mii.hpp"

class ConfigDataMii : public BinaryDataChunkBase {
public:
    // A file's icon: its Mii, or one of the fellows (Mario first, as FileSelectIconID::EFellowID + 1).
    enum IconId {
        IconId_Mii = 0,
        IconId_Mario = 1,
        IconId_Luigi = 2,
        IconId_Yoshi = 3,
        IconId_Kinopio = 4,
        IconId_Peach = 5,
    };

    ConfigDataMii();

    virtual u32 makeHeaderHashCode() const override;
    virtual u32 getSignature() const override;
    virtual s32 serialize(u8*, u32) const override;
    virtual s32 deserialize(const u8*, u32) override;
    virtual void initializeData() override;

    void setMiiOrIconId(const void*, const u32*);
    bool getIconId(u32*) const;
    bool getMiiId(void*) const;

private:
    // Saved bits. A chunk without the icon byte uses its Mii only when Flag_UseMii is set.
    enum Flag {
        Flag_UseMii = 1,
        Flag_MiiChosen = 2,
    };

    u8 mFlag;
    u8 mIconId;
    Mii::Id mMiiId;
};
