#pragma once

#include "Game/Screen/LayoutActor.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Screen/MovieStream.hpp"

// The scene's movie: as large as the screen holds it at its own shape, centred, black around it; its time is its
// sound's, or the game's steps for a movie with none.
class MoviePlayerSimple : public LayoutActor, public ArenaOwned {
public:
    MoviePlayerSimple();
    virtual ~MoviePlayerSimple();

    virtual void init(const TableIter&);
    virtual void draw() const;
    virtual void movement();

    void startMovie(const char* pName, bool loop);
    void stopMovie();
    bool isMovieActive() const;
    bool isMoviePlaying() const;
    // The frame on the screen (-1 before the first), and the movie's frames.
    s32 getCurrentFrame() const;
    s32 getTotalFrame() const;
    // The name startMovie was given (the caller's pointer), "" before any.
    const char* getMovieName() const { return mMovieName; }
    void setVolume(f32, s32);
    void exeSimpleOff();
    void exeOpen();
    void exePlaying();
    void exeSimpleSuspend();

private:
    void play(bool loop);
    void stop();
    void drawBands(f32 x, f32 y, f32 width, f32 height) const;

    const char* mMovieName;
    MovieStream* mStream;
    bool mLoop;
    // Whether input was being checked before the movie took it, for when it ends.
    bool mInputCheckedBefore;
};
