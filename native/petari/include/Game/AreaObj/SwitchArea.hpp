#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class SwitchArea : public AreaObj {
public:
    SwitchArea(int, const char*);

    virtual void init(const TableIter&);
    virtual void movement();

    virtual const char* getManagerName() const;

    bool isUpdate() const;
};
