#pragma once

#include "Game/Screen/LayoutActor.hpp"
#include <JSystem/JKernel/JKRDisposer.hpp>
#include "Game/Screen/MovieStream.hpp"
class NebulaArena;

struct Movie {
    const char* mMovieName;
    MovieVideoInfo mVideoInfo;
    u8* mBuffer;
    u32 mCurrentFrame;
    bool _18;
    bool _19;
    s32 mFrameRateDefault;
    s32 _20;
    u32 _24;
};

class MoviePlayerSimple : public LayoutActor, public JKRDisposer {
public:
    /// @brief Creates a new `MoviePlayerSimple`.
    MoviePlayerSimple();

    /// @brief Destroys the `MoviePlayerSimple`.
    virtual ~MoviePlayerSimple();

    virtual void init(const JMapInfoIter&);
    virtual void draw() const;

    void startMovie(const char*, bool);
    void stopMovie();
    bool isMovieActive() const;
    bool isMoviePlaying() const;
    s32 getCurrentFrame() const;
    s32 getTotalFrame() const;
    void setFrameRateDefault();
    void setVolume(f32, s32);
    void setUnpauseHomeButtonFlag();
    void exeSimpleOff();
    void exeOpen();
    void exePlaying();
    void exeSimpleSuspend();
    void play(bool);
    void stop();
    void drawCinemaFrame() const;

private:
    Movie* mMovie;
    NebulaArena* mHeap;
    MovieStream* mStream;
    bool _44;
    bool _45;
};
