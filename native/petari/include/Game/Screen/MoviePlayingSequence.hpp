#pragma once

#include "Game/Screen/LayoutActor.hpp"
#include "Game/Util/Array.hpp"

class DemoPadRumbler;
class MovieSubtitles;

/// @brief The general parameters for a movie.
struct MoviePlayingInfo {
    /// @brief A pointer to the null-terminated absolute path of the associated movie file.
    const char* mMovieName;

    /// @brief A pointer to the null-terminated absolute path of the associated movie file for Luigi.
    const char* mMovieNameLuigi;

    /// @brief A pointer to the null-terminated name of the galaxy to enter after playback.
    const char* mStageName;

    /// @brief A pointer to the null-terminated name of the background music to start after playback.
    const char* mStageBgmName;

    /// @brief The number of frames to wait on a blank screen before playback.
    s32 mPlayWaitTime;

    /// @brief The number of frames to wait on a blank screen after playback.
    s32 mEndWaitTime;

    /// @brief The type of screen transition for the start of playback.
    s32 mOpenWipeType;

    /// @brief The number of frames for the screen to transition at the start of playback.
    s32 mOpenWipeTime;

    /// @brief The type of screen transition for the end of playback.
    s32 mCloseWipeType;

    /// @brief The number of frames for the screen to transition at the end of playback.
    s32 mCloseWipeTime;

    /// @brief The type of screen transition for after playback.
    s32 mEndWaitWipeType;

    /// @brief The loudness of the audio within the unit interval.
    f32 mVolume;
};

class MoviePlayingSequence : public LayoutActor {
public:
    // Indexes the movie list in this order.
    enum MovieType {
        MovieType_PrologueA,
        MovieType_PrologueB,
        MovieType_FinalBattle,
        MovieType_EpilogueA,
        MovieType_EpilogueB,
        MovieType_EndingA,
        MovieType_EndingB,
    };

    MoviePlayingSequence(const char*, s32);

    virtual void appear() override;
    virtual void kill() override;

    static const char* getMovieName(MovieType);

    void exeWait();
    void exePlayWait();
    void exePlayStart();
    void exePlay();
    bool tryStartSubtitles();
    bool tryEnd();
    bool trySkip();
    void exeCloseWipeOnPlaying();
    void exeEndWait();

private:
    const MoviePlayingInfo* mInfo;
    MR::Vector< MR::AssignableArray< MovieSubtitles* > > mSubtitles;
    DemoPadRumbler* mPadRumbler;
};

class MoviePlayingSequenceHolder : public NameObj {
public:
    /// @brief Creates a new `MoviePlayingSequenceHolder`.
    /// @param pName A pointer to the null-terminated name of the object.
    MoviePlayingSequenceHolder(const char* pName);

    MoviePlayingSequence* getSequence(int) const;
    s32 getSequenceNum() const;

private:
    MR::AssignableArray< MoviePlayingSequence* > mSequence;
};

namespace MR {
    void createMoviePlayingSequence();
    void startMovie(int);
    bool isEndMovie(int);
    void startMovieEpilogueA();
    void startMovieEndingA();
    void startMovieEndingB();
    bool isEndMovieEpilogueA();
    bool isEndMovieEndingA();
    bool isEndMovieEndingB();
    bool isMoviePlayingOnSequence();
};  // namespace MR
