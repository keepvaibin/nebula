#pragma once

// A bool's level and its on and off edges between updates.
class TriggerChecker {
public:
    TriggerChecker();

    void update(bool input);
    bool getLevel() const;
    bool getOnTrigger() const;
    bool getOffTrigger() const;

private:
    bool mPrevLevel;
    bool mCurrLevel;
};
