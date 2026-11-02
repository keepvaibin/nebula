#pragma once

#include "Game/NameObj/NameObj.hpp"

class NPCCapsParameterReader;
class NPCItemParameterReader;
class ResourceHolder;

class NPCDirector : public NameObj {
public:
    /// @brief Creates a new `NPCDirector`.
    NPCDirector();

    virtual void init(const JMapInfoIter& rIter);
    
    NPCCapsParameterReader* mCapsParameterReader;
    NPCItemParameterReader* mItemParameterReader;
    ResourceHolder* mDataResourceHolder;
};
