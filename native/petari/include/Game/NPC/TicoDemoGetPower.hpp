#pragma once

#include "Game/System/NerveExecutor.hpp"

class TableIter;
class Tico;

class TicoDemoGetPower : public NerveExecutor {
public:
    TicoDemoGetPower(Tico*, const TableIter&);

    void startDemo();

    void exeWait();
    void exeDemo();

    Tico* mHost;
};
