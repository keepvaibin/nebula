// The function facts' fixture: what the rows are (see functions_fixture.hpp).
#include "functions_fixture.hpp"

FnBase::~FnBase() {}
void FnBase::f(int) {}
int FnBase::g() const { return 1; }
int FnBase::s() { return 2; }
int FnBase::plain() const { return 3; }
void FnMid::f(int) {}
FnDerived::FnDerived() {}
FnDerived::~FnDerived() {}
void FnDerived::f(int) {}
int FnDerived::g() const { return 4; }
void FnDerived::h() {}
void FnFromT::t(int) {}
void FnNs::Inner::m() {}
int fnFree(int x) { return x; }

namespace {
struct Local : FnBase {  // a record with no name a mod can write: its functions have no owner
    int g() const override { return 5; }
};
}  // namespace

FnBase* fnMake() { return new Local(); }
