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
    void requestMountModelChangableObjArchives(const char*, s32);
    bool isReadResourceFromDVDAtModelChangableObj(const char*, s32);
};  // namespace MR
