// The placement facts' fixture: a factory table of the game's form (a unit named NameObj/NameObjFactory.cpp), one row
// per kind of creator: a template instance, a function whose body is here, one whose body is in the other unit, one
// whose body is in none.
#include "../places_fixture.hpp"

namespace {
    template < typename T >
    PfObj* createNameObj(const char*) {
        return new T();
    }
    PfObj* createBravo(const char*) {
        return new PfBravo();
    }
    PfObj* createElsewhere(const char*);  // its body is in no unit here
    struct PfEntry {
        const char* mName;
        PfObj* (*mCreate)(const char*);
    };
    const PfEntry cCreateTable[] = {
        {
            "PfAlpha",
            createNameObj< PfAlpha >,
        },
        {
            "PfBravo",
            createBravo,
        },
        {
            "PfCharlie",
            pfCreateCharlie,
        },
        {
            "PfElsewhere",
            createElsewhere,
        },
    };
}  // namespace

PfObj* pfCreate(int index) {
    return cCreateTable[index].mCreate(cCreateTable[index].mName);
}
