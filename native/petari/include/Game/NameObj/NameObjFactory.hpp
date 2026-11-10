#pragma once

#include "Game/System/LumaCore.hpp"

typedef NameObj* (*CreatorFuncPtr)(const char*);

namespace {
    template < typename T >
    NameObj* createNameObj(const char* pName) {
        T* const pObj = new T(pName);
        // Its exact class for mods (the class rows are generated from the headers: tools/lumacore).
        lumacore::setClass(pObj, lumacore::kClassId< T >);
        return pObj;
    }
};  // namespace

/// @brief A namespace for creating `NameObj` instances.
namespace NameObjFactory {
    struct Name2CreateFunc {
        const char* mName;
        CreatorFuncPtr mCreateFunc;
    };

    CreatorFuncPtr getCreator(const char*);
    bool isPlayerArchiveLoaderObj(const char*);
    const Name2CreateFunc* getName2CreateFunc(const char* pName);
};  // namespace NameObjFactory
