#pragma once


class PlayerInput;

/// @brief The wrapper around the state of a player's buttons.
class InputButtons {
public:
    /// @param pPad The owning player's input.
    InputButtons(const PlayerInput* pPad);

    /// @brief Updates the state of the player's buttons.
    void update();

    /// @brief Determines if up on the +Control Pad is pressed.
    /// @return `true` if up is pressed, `false` otherwise.
    bool testButtonUp() const;

    /// @brief Determines if down on the +Control Pad is pressed.
    /// @return `true` if down is pressed, `false` otherwise.
    bool testButtonDown() const;

    /// @brief Determines if left on the +Control Pad is pressed.
    /// @return `true` if left is pressed, `false` otherwise.
    bool testButtonLeft() const;

    /// @brief Determines if right on the +Control Pad is pressed.
    /// @return `true` if right is pressed, `false` otherwise.
    bool testButtonRight() const;

    /// @brief Determines if the A Button is pressed.
    /// @return `true` if the A Button is pressed, `false` otherwise.
    bool testButtonA() const;

    /// @brief Determines if the B Button is pressed.
    /// @return `true` if the B Button is pressed, `false` otherwise.
    bool testButtonB() const;

    /// @brief Determines if the Minus (-) Button is pressed.
    /// @return `true` if the Minus (-) Button is pressed, `false` otherwise.
    bool testButtonMinus() const;

    /// @brief Determines if the Plus (+) Button is pressed.
    /// @return `true` if the Plus (+) Button is pressed, `false` otherwise.
    bool testButtonPlus() const;

    /// @brief Determines if the 1 Button is pressed.
    /// @return `true` if the 1 Button is pressed, `false` otherwise.
    bool testButton1() const;

    /// @brief Determines if the 2 Button is pressed.
    /// @return `true` if the 2 Button is pressed, `false` otherwise.
    bool testButton2() const;

    /// @brief Determines if the C Button is pressed.
    /// @return `true` if the C Button is pressed, `false` otherwise.
    bool testButtonC() const;

    /// @brief Determines if the Z Button is pressed.
    /// @return `true` if the Z Button is pressed, `false` otherwise.
    bool testButtonZ() const;

    /// @brief Determines if up on the +Control Pad was just pressed.
    /// @return `true` if up was just pressed, `false` otherwise.
    bool testTriggerUp() const;

    /// @brief Determines if down on the +Control Pad was just pressed.
    /// @return `true` if down was just pressed, `false` otherwise.
    bool testTriggerDown() const;

    /// @brief Determines if left on the +Control Pad was just pressed.
    /// @return `true` if left was just pressed, `false` otherwise.
    bool testTriggerLeft() const;

    /// @brief Determines if right on the +Control Pad was just pressed.
    /// @return `true` if right was just pressed, `false` otherwise.
    bool testTriggerRight() const;

    /// @brief Determines if the A Button was just pressed.
    /// @return `true` if the A Button was just pressed, `false` otherwise.
    bool testTriggerA() const;

    /// @brief Determines if the B Button was just pressed.
    /// @return `true` if the B Button was just pressed, `false` otherwise.
    bool testTriggerB() const;

    /// @brief Determines if the Minus (-) Button was just pressed.
    /// @return `true` if the Minus (-) Button was just pressed, `false` otherwise.
    bool testTriggerMinus() const;

    /// @brief Determines if the HOME Button was just pressed.
    /// @return `true` if the HOME Button was just pressed, `false` otherwise.
    bool testTriggerHome() const;

    /// @brief Determines if the Plus (+) Button was just pressed.
    /// @return `true` if the Plus (+) Button was just pressed, `false` otherwise.
    bool testTriggerPlus() const;

    /// @brief Determines if the 1 Button was just pressed.
    /// @return `true` if the 1 Button was just pressed, `false` otherwise.
    bool testTrigger1() const;

    /// @brief Determines if the 2 Button was just pressed.
    /// @return `true` if the 2 Button was just pressed, `false` otherwise.
    bool testTrigger2() const;

    /// @brief Determines if the C Button was just pressed.
    /// @return `true` if the C Button was just pressed, `false` otherwise.
    bool testTriggerC() const;

    /// @brief Determines if the Z Button was just pressed.
    /// @return `true` if the Z Button was just pressed, `false` otherwise.
    bool testTriggerZ() const;

    /// @brief Determines if the Z Button was just released.
    /// @return `true` if the Z Button was just released, `false` otherwise.
    bool testReleaseZ() const;

    /// @brief Determines if any button was just pressed or released.
    /// @return `true` if any button was just pressed or released, `false` otherwise.

private:
    /// @brief The owning player's input.
    const PlayerInput* mPad;

    /// @brief The combination of buttons pressed.
    u32 mHold;

    /// @brief The combination of buttons just pressed.
    u32 mTrigger;

    /// @brief The combination of buttons just released.
    u32 mRelease;
    // Presses and releases in frames the world clock gave no step (a rate below real time), given with the next
    // step's, so no tap is lost; the system's screens read each frame's own (services-time 4, J22-3).
    u32 mCarriedTrigger;
    u32 mCarriedRelease;



};
