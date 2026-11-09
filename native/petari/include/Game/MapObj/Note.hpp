#pragma once

#include "Game/LiveActor/FlashingCtrl.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"

class NoteFairy;

/// @brief Class that represents a single note in a NoteFairy actor.
class Note : public LiveActor {
public:
    Note(const char*, const Vec3f&, NoteFairy*);

    virtual void init(const TableIter&);
    virtual void control();
    virtual void calcAndSetBaseMtx();

    /// @brief Checks if the sensor is either a player or a rideable entity and if it is, it will "collect" the note.
    virtual void attackSensor(HitSensor*, HitSensor*);

    void startCountDown();

    /// @brief Begins the countdown sequence that plays the "waiting" animation, and is switched to the countdown nerve when mIsCountdown is set to
    /// true by the Note::mParentFairy.
    void exeWait();

    /// @brief Begins the flashing sequence and if the current step reaches Note::mCounter's value, it calls Note::kill().
    void exeCountDown();

    /// @brief Scales the note's velocity by -30.0 and then invalidates all hit sensors, and after 10 steps, calls Note::kill().
    void exeFlyUp();

    NoteFairy* mParentFairy;  ///< The parent NoteFairy that this note was spawned from.
    Vec3f mRailDirection;
    int mCounter;  ///< The amount of steps it takes for the note to disappear.
    f32 mBrkFrame;
    bool mIsCountdown;
    FlashingCtrl* mFlashCtrl;  // A pointer to the FlashingCtrl instance that handles the "fading away" effects.
};

class NoteGroup : public LiveActorGroup {
public:
    NoteGroup();

    virtual void init(const TableIter&) override;
    virtual void movement() override;

    f32 mRotation;
};
