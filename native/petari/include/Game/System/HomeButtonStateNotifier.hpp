#pragma once

class MoviePlayerSimple;

class HomeButtonStateNotifier {
public:
    HomeButtonStateNotifier();

    void update(bool);
    void registerMoviePlayerSimple(MoviePlayerSimple* pMoviePlayer);
    void unregisterMoviePlayerSimple(MoviePlayerSimple* pMoviePlayer);
    void notifyHomeButtonDeactive();

private:
    bool mWasActive;
    MoviePlayerSimple* mMoviePlayer;
};
