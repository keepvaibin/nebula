// The needs fixture (tools/lumacore/needs_fixture_test.py, M2): the scene's objects as the game reaches them, and one
// function per rule of the needs pass (instrument.py Needs, gen_types.py merge_needs).
#pragma once

enum {
    SceneObj_Holder = 1,
    SceneObj_Counter = 2,
    SceneObj_Made = 3,
};

struct NdObj {
    int x;
};
struct NdActor {
    int x;
};
struct NdHolder {
    NdActor* actor;
};
// A game system object whose parts are made after it (SaveDataHandleSequence): the hand table's class row.
struct NdFile {
    int* data;
    NdFile();
    void init();
    int use();
};

namespace MR {
    void* getSceneObjHolder();
    bool isExistSceneObj(int id);
    NdObj* createSceneObj(int id);
    template <class T>
    T* getSceneObj(int id) {
        return static_cast<T*>(getSceneObjHolder()) + id;
    }
}  // namespace MR
