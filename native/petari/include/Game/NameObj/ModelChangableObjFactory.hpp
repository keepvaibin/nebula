#pragma once


class NameObj;

typedef NameObj* (*CreatorFuncPtr)(const char*);

struct Model2CreateFunc {
    const char* mNamePrefix;  // set: matches names that start with it; null: mArchiveName exactly
    const char* mArchiveName;
    CreatorFuncPtr mCreatorFunc;
};

namespace MR {
    CreatorFuncPtr getModelChangableObjCreator(const char*);
};  // namespace MR
