#pragma once

#include "Game/MapObj/GeneralMapParts.hpp"

class ClipFieldMapParts : public GeneralMapParts {
public:
    ClipFieldMapParts(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;
    virtual void connectToScene() override;
};
