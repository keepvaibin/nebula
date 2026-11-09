#pragma once

class NameObj;

typedef NameObj* (*CreatorFuncPtr)(const char*);

namespace {
    template < typename T >
    NameObj* createNameObj(const char* pName) {
        return new T(pName);
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
