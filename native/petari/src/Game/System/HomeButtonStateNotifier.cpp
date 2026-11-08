#include "Game/System/HomeButtonStateNotifier.hpp"
#include "Game/Screen/MoviePlayerSimple.hpp"

HomeButtonStateNotifier::HomeButtonStateNotifier() : mWasActive(false), mMoviePlayer(nullptr) {
}

void HomeButtonStateNotifier::update(bool isActive) {
    if (mWasActive && !isActive) {
        notifyHomeButtonDeactive();
    }

    mWasActive = isActive;
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
