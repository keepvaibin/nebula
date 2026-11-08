#pragma once

#include "Game/System/NebulaCalendar.hpp"
#include "Game/System/SaveFileId.hpp"

class ConfigDataHolder;
class GameDataHolder;

class UserFile {
public:
    UserFile();

    bool isCreated() const;
    s32 getPowerStarNum() const;
    s32 getStarPieceNum() const;
    s32 getPlayerMissNum() const;
    bool getMiiId(void*) const;
    bool getIconId(u32*) const;
    bool isLastLoadedMario() const;
    bool isOnCompleteEndingMarioAndLuigi() const;
    NebulaCalendar::SaveTime getLastModified() const;
    void setCreated();
    void setMiiOrIconId(const void*, const u32*);
    void setLastLoadedMario(bool);
    void onCompleteEndingCurrentPlayer();
    void updateLastModified();
    void setUserName(const char16_t*);
    SaveFileId getGameDataFile() const;
    u32 makeGameDataBinary(u8*, u32) const;
    void loadFromGameDataBinary(SaveFileId, const u8*, u32);
    SaveFileId getConfigDataFile() const;
    u32 makeConfigDataBinary(u8*, u32) const;
    void loadFromConfigDataBinary(SaveFileId, const u8*, u32);
    void resetAllData();
    bool isViewNormalEnding() const;
    bool isViewCompleteEnding() const;
    bool isPowerStarGetFinalChallengeGalaxy() const;

    GameDataHolder* mGameDataHolder;
    ConfigDataHolder* mConfigDataHolder;
    bool mIsPlayerMario;
    bool mIsGameDataCorrupted;
    bool mIsConfigDataCorrupted;
    char16_t* mUserName;
};
