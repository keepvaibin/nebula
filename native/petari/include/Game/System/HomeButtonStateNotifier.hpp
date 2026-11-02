#pragma once

class MoviePlayerSimple;

class HomeButtonStateNotifier {
public:
    /// @brief Creates a new `HomeButtonStateNotifier`.
    HomeButtonStateNotifier();

    void update(bool);
    void registerMoviePlayerSimple(MoviePlayerSimple* pMoviePlayer);
    void unregisterMoviePlayerSimple(MoviePlayerSimple* pMoviePlayer);
    void notifyHomeButtonDeactive();

private:
    bool _0;
    MoviePlayerSimple* mMoviePlayer;
};
