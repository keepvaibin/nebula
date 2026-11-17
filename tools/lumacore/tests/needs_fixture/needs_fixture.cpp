// The needs fixture: what each function needs (see needs_fixture.hpp); needs_fixture_test.py holds the expected facts.
#include "needs_fixture.hpp"

// Tested, then returned: null when the holder is not there; needs nothing.
NdHolder* ndHolder() {
    return MR::isExistSceneObj(SceneObj_Holder) ? MR::getSceneObj< NdHolder >(SceneObj_Holder) : nullptr;
}

// A pointer from a tested one: null when the holder is not there.
NdActor* ndActor() {
    NdHolder* holder = ndHolder();
    return holder != nullptr ? holder->actor : nullptr;
}

// Through a null one: needs the holder.
void ndUseActor() {
    ndActor()->x = 1;
}

// A caller of one that needs: the same needs.
void ndDeep() {
    ndUseActor();
}

// A test only: needs nothing, and is null when nothing (it gives no pointer).
bool ndHasActor() {
    return ndActor() != nullptr;
}

// Unchecked: needs the counter and the scene's holder of objects.
void ndRaw() {
    MR::getSceneObj< NdObj >(SceneObj_Counter)->x = 1;
}

// Checked before by an early return: needs nothing.
void ndGuarded() {
    if (!MR::isExistSceneObj(SceneObj_Counter)) {
        return;
    }
    MR::getSceneObj< NdObj >(SceneObj_Counter)->x = 2;
}

// Made first: needs the scene's holder only.
void ndMakes() {
    MR::createSceneObj(SceneObj_Made);
    MR::getSceneObj< NdObj >(SceneObj_Made)->x = 3;
}

// A callee makes it: needs the scene's holder only.
void ndCallsMakes() {
    ndMakes();
    MR::getSceneObj< NdObj >(SceneObj_Made)->x = 4;
}

// Unchecked and returned: needs the scene's holder, null when the counter is not there.
NdObj* ndCounter() {
    return MR::getSceneObj< NdObj >(SceneObj_Counter);
}

// Uses nothing of the scene.
int ndPlain(int x) {
    return x + 1;
}

// A class whose part is made after it: by the hand table's class row, its use needs the file; its making does not.
NdFile::NdFile() : data(nullptr) {}

void NdFile::init() {
    data = new int(0);
}

int NdFile::use() {
    return *data;
}

// A caller of the class's use: the same need.
int ndCallsFile(NdFile* pFile) {
    return pFile->use();
}
