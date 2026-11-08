#pragma once

// A bool's level and its on and off edges between updates.
class TriggerChecker {
public:
    TriggerChecker();

    void update(bool input);
    // Sets both levels, so no edge follows.
    void setInput(bool input);
    bool getLevel() const;
    bool getOnTrigger() const;
    bool getOffTrigger() const;

private:
    bool mPrevLevel;
    bool mCurrLevel;
};
