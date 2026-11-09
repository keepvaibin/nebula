#pragma once

#include "Game/Boss/DodoryuStateBase.hpp"

class DodoryuStateWait : public DodoryuStateBase {
public:
    DodoryuStateWait(Dodoryu* pHost, const char* pName);

    virtual void init(const TableIter& rIter) override;

    void exeWait();

private:
    s32 mNextStateCounter;
};
