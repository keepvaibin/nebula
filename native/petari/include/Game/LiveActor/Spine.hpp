#pragma once


class ActorStateKeeper;
class Nerve;

class Spine {
public:
    Spine(void*, const Nerve*);

    void update();
    void setNerve(const Nerve*);
    const Nerve* getCurrentNerve() const;
    void changeNerve();
    void initStateKeeper(int);

    bool isCurrentNerve(const Nerve* pNerve) const {
        return getCurrentNerve() == pNerve;
    }

    void* mExecutor;
    const Nerve* mCurrNerve;
    const Nerve* mNextNerve;
    s32 mStep;
    ActorStateKeeper* mStateKeeper;
};
