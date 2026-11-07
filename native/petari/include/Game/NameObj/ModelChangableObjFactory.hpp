#pragma once


class NameObj;

typedef NameObj* (*CreatorFuncPtr)(const char*);

struct Model2CreateFunc {
    const char* _0;
    const char* mArchiveName;
    CreatorFuncPtr mCreatorFunc;
};

namespace MR {
    CreatorFuncPtr getModelChangableObjCreator(const char*);
};  // namespace MR
