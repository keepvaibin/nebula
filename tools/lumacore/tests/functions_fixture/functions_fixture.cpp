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

void fnOut(LUMA_OUT FnVec*);  // a redeclaration with the same marks
void fnOut(FnVec* out) { out->x = 1; }
void fnIn(const float* value) { (void)value; }
void fnInout(FnColor* color) { *color = FnBlue; }
void fnKept(int* registered) { (void)registered; }
void fnCount(const float* p, int n, FnVec* out) {
    if (out) {
        out->x = p[n - 1];
    }
}
void fnFixed(float* m) { m[11] = 0; }
void fnInside(const FnV2<float>* p, FnVec4* q) { q->x = p->x; }
void fnDefined(LUMA_OUT FnVec* out) { out->y = 2; }
void FnMarked::get(FnVec* out) const { out->z = 3; }
void fnByMacro(float* v) { *v = 1; }

// Self-description: an array table's keys, each row's leading string; none when a row has none.
struct FnKeyRow {
    const char* name;
    int value;
};
[[maybe_unused]] static FnKeyRow sFnKeyed[] = {{"Alpha", 1}, {"Beta", 2}};
[[maybe_unused]] static const char* sFnNames[] = {"One", "Two", "Three"};
[[maybe_unused]] static FnKeyRow sFnUnkeyed[] = {{"A", 1}, {nullptr, 2}};
[[maybe_unused]] static FnKeyRow sFnShort[3] = {{"A", 1}, {"B", 2}};

// Variadic functions (docs/modding/language/varargs.md): a va_list function, a variadic one handing its rest to it, one
// handing them to another variadic one, a constexpr one (a call row after it), a head that begins with a macro.
#include <cstdarg>
int fnVaList(const char* pFormat, va_list args) {
    int n = 0;
    while (*pFormat++ != 0) {
        n += va_arg(args, int);
    }
    return n;
}
int fnVariadic(const char* pFormat, ...) {
    va_list args;
    va_start(args, pFormat);
    const int n = fnVaList(pFormat, args);
    va_end(args);
    return n;
}
int fnVariadicAgain(int x, const char* pFormat, ...) { return x + fnVariadic(pFormat, 1, 2); }
constexpr int fnConstexpr(int x) { return x * 3 + 1; }
static_assert(fnConstexpr(2) == 7, "evaluated while compiling");
#define FN_EXPORT __attribute__((used))
extern "C" {
FN_EXPORT int fnExported(int x) { return x + 1; }
}
