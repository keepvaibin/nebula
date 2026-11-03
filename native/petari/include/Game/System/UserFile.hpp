#pragma once

#include "Game/System/NebulaCalendar.hpp"

class ConfigDataHolder;
class GameDataHolder;

class UserFile {
public:
    /// @brief Creates a new `UserFile`.
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
    const char* getGameDataName() const;
    void makeGameDataBinary(u8*, u32) const;
    void loadFromGameDataBinary(const char*, const u8*, u32);
    const char* getConfigDataName() const;
    void makeConfigDataBinary(u8*, u32) const;
    void loadFromConfigDataBinary(const char*, const u8*, u32);
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
