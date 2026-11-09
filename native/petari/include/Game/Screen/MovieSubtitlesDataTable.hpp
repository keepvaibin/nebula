#pragma once


/// @brief The general parameters for a subtitle.
struct SubtitlesMessageInfo {
    /// @brief A pointer to the null-terminated name of the subtitle message.
    const char* mMessageID;

    /// @brief The number of frames to wait before displaying the subtitle.
    s32 mStartStep;

    /// @brief The number of frames to display the subtitle for.
    s32 mAppearTime;
};

/// @brief The general parameters for a movie's subtitles.
struct SubtitlesInfo {
    /// @brief A pointer to the null-terminated absolute path of the associated movie file.
    const char* mMovieName;

    /// @brief An array of general parameters for each subtitle.
    const SubtitlesMessageInfo* mMessageInfo;

    /// @brief The number of subtitles in the array.
    s32 mMessageNum;
};

namespace MovieSubtitlesUtil {
    const SubtitlesMessageInfo* getSubtitlesMessageInfo(const char*, s32);
    bool isExistSubtitles(const char*, s32);
    const char* getSubtitlesMessageId(const char*, s32);
    s32 getSubtitlesStartStep(const char*, s32);
    s32 getSubtitlesAppearTime(const char*, s32);
    s32 getSubtitlesMessageNum(const char*);
};  // namespace MovieSubtitlesUtil
