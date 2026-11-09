#pragma once

#include "Game/Screen/WipeLayoutBase.hpp"

class WipeGameOver : public WipeLayoutBase {
public:
    WipeGameOver();

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual void wipe(s32) override;
    virtual void forceClose() override;
    virtual void forceOpen() override;
    virtual bool isOpen() const override;
    virtual bool isClose() const override;
    virtual bool isWipeIn() const override;
    virtual bool isWipeOut() const override;

    virtual s32 getWipeType() const override {
        return 2;
    }

    void exeActive();
    void exeWait();
};
