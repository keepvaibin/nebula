// SPDX-License-Identifier: GPL-3.0-only
// lumacore's classes of living objects (Game/System/LumaCore.cpp): classOf gives the id a constructor named, and
// exactClassOf (the plain module's) gives it only when the object's vtable is the one recorded for that id, so an object
// of a class whose constructor never calls setClass gets 0, not its base's id. The class table is the test's; NameObj's
// members are the test's (the game's need the scene).
#include "Game/NameObj/NameObj.hpp"
#include "Game/System/LumaCore.hpp"

#include <stdio.h>

NameObj::NameObj(const char* pName) : mName(pName), mMovementFlag(), mLumaClass(0), mExecutorIdx(-1) {}
NameObj::~NameObj() {}
void NameObj::init(const TableIter&) {}
void NameObj::initAfterPlacement() {}
void NameObj::movement() {}
void NameObj::draw() const {}
void NameObj::calcAnim() {}
void NameObj::calcViewAndEntry() {}

namespace {
    enum : u16 { cUnknown, cNameObj, cBase, cRecorded };
}  // namespace

namespace lumacore {
    extern const ClassRow kClassRows[];
    extern const u16 kClassRowCount;
    const ClassRow kClassRows[] = {
        {"unknown", 0, 0, nullptr},
        {"NameObj", 0, sizeof(NameObj), nullptr},
        {"Base", cNameObj, 0, nullptr},
        {"Recorded", cBase, 0, nullptr},
    };
    const u16 kClassRowCount = 4;
}  // namespace lumacore

namespace {
    int sFailures = 0;

    void check(bool ok, const char* pWhat) {
        if (!ok) {
            fprintf(stderr, "FAILED: %s\n", pWhat);
            sFailures++;
        }
    }

    class Base : public NameObj {
    public:
        Base() : NameObj("Base") { lumacore::setClass(this, cBase); }
        void movement() override {}
    };

    // A class whose constructor names its id.
    class Recorded : public Base {
    public:
        Recorded() { lumacore::setClass(this, cRecorded); }
        void movement() override {}
    };

    // A class whose constructor never names one (it has no row of its own: 74 of the game's rows are such, and a
    // template's instances).
    class Unrecorded : public Base {
    public:
        void movement() override {}
    };
}  // namespace

int main() {
    Base base;
    Recorded recorded;
    Unrecorded unrecorded;
    check(lumacore::classOf(&base) == cBase, "classOf: a Base is Base");
    check(lumacore::classOf(&recorded) == cRecorded, "classOf: a Recorded is Recorded");
    check(lumacore::classOf(&unrecorded) == cBase, "classOf: an Unrecorded keeps its base's id");
    check(lumacore::exactClassOf(&base) == cBase, "exactClassOf: a Base is Base");
    check(lumacore::exactClassOf(&recorded) == cRecorded, "exactClassOf: a Recorded is Recorded");
    check(lumacore::exactClassOf(&unrecorded) == cUnknown, "exactClassOf: an Unrecorded is no class");
    // Made again: the vtables already recorded change nothing.
    Unrecorded again;
    Base another;
    check(lumacore::exactClassOf(&again) == cUnknown, "exactClassOf: a second Unrecorded is no class");
    check(lumacore::exactClassOf(&another) == cBase, "exactClassOf: a second Base is Base");
    check(lumacore::exactClassOf(nullptr) == cUnknown, "exactClassOf: no object is no class");
    if (sFailures == 0) {
        printf("native_lumacore_class_tests: all passed\n");
    }
    return sFailures == 0 ? 0 : 1;
}
