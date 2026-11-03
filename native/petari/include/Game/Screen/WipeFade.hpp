#pragma once

#include "Game/Screen/WipeLayoutBase.hpp"
#include "Game/Util/Color.hpp"

class WipeFade : public WipeLayoutBase {
public:
    WipeFade(const char*, const Color8&);

    virtual void init(const JMapInfoIter&);
    virtual void draw() const;
    virtual void control();
    virtual void wipe(s32);
    virtual void forceClose();
    virtual void forceOpen();
    virtual bool isOpen() const;
    virtual bool isClose() const;
    virtual bool isWipeIn() const;
    virtual bool isWipeOut() const;

    void setColor(NebulaColor8 color) {
        NebulaColor8 copy = color;
        mFillColor = copy;
    }

private:
    bool mIsWipeIn;
    s32 mFrame;
    s32 mStep;
    NebulaColor8 mFillColor;
};
