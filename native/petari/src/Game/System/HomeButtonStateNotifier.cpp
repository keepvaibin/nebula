#include "Game/System/HomeButtonStateNotifier.hpp"
#include "Game/Screen/MoviePlayerSimple.hpp"

HomeButtonStateNotifier::HomeButtonStateNotifier() : mWasActive(false), mMoviePlayer(nullptr) {
}

void HomeButtonStateNotifier::update(bool param1) {
    if ((mWasActive || !param1) && mWasActive && !param1) {
        notifyHomeButtonDeactive();
    }

    mWasActive = param1;
}

void HomeButtonStateNotifier::registerMoviePlayerSimple(MoviePlayerSimple* pMoviePlayer) {
    mMoviePlayer = pMoviePlayer;
}

void HomeButtonStateNotifier::unregisterMoviePlayerSimple(MoviePlayerSimple* pMoviePlayer) {
    mMoviePlayer = nullptr;
}

void HomeButtonStateNotifier::notifyHomeButtonDeactive() {
    if (mMoviePlayer == nullptr) {
        return;
    }

    if (!mMoviePlayer->isMovieActive()) {
        return;
    }

    mMoviePlayer->setUnpauseHomeButtonFlag();
}
