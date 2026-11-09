#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class SwitchArea : public AreaObj {
public:
    SwitchArea(int, const char*);

    virtual void init(const TableIter&) override;
    virtual void movement() override;

    virtual const char* getManagerName() const override;

    bool isUpdate() const;
};
