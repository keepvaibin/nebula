#pragma once

#include "Game/NameObj/NameObj.hpp"

class ResourceShare : public NameObj {
public:
    /// @brief Creates a new `ResourceShare`.
    ResourceShare();

    virtual ~ResourceShare();

    void* _C;
    void* _10;
    u32 _14;
};
