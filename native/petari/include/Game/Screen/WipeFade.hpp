#pragma once

#include "Game/Screen/WipeLayoutBase.hpp"
#include "Game/Util/Color.hpp"

class WipeFade : public WipeLayoutBase {
public:
    WipeFade(const char*, const Color8&);

    virtual void init(const TableIter&) override;
    virtual void draw() const override;
    virtual void control() override;
    virtual void wipe(s32) override;
    virtual void forceClose() override;
    virtual void forceOpen() override;
    virtual bool isOpen() const override;
    virtual bool isClose() const override;
    virtual bool isWipeIn() const override;
    virtual bool isWipeOut() const override;

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
