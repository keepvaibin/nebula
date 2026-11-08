#pragma once

#include "Game/MapObj/GeneralMapParts.hpp"

class ClipFieldMapParts : public GeneralMapParts {
public:
    ClipFieldMapParts(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void control();
    virtual void connectToScene();
};
