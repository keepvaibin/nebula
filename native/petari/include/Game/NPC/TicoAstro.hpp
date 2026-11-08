#pragma once

#include "Game/NPC/Tico.hpp"

class TicoAstro : public Tico {
public:
    TicoAstro(const char*);

    virtual void init(const TableIter&);
};
