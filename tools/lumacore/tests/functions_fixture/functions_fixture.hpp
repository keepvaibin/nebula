// The function facts' fixture (tools/lumacore/functions_fixture_test.py, the checker design's C-1): each kind of row,
// virtual, pure and const, and overrides through one base, two bases, a destructor and a template's instance; and
// C-6's parameter marks, one function per mark.
#pragma once

#include "Game/LumaMarks.hpp"

struct FnBase {
    virtual ~FnBase();
    virtual void f(int);
    virtual int g() const = 0;  // pure, with a body in the unit
    static int s();
    int plain() const;
};
struct FnMid : FnBase {
    void f(int) override;
};
struct FnOther {
    virtual void h();  // declared, defined nowhere here: an override names it, and it is no row
};
struct FnDerived : FnMid, FnOther {
    FnDerived();
    ~FnDerived() override;
    void f(int) override;
    int g() const override;
    void h() override;
};
template <class T>
struct FnT {
    virtual void t(T);
};
struct FnFromT : FnT<int> {
    void t(int) override;  // overrides a template instance's function: named by the instance's row name
};
namespace FnNs {
struct Inner {
    void m();
};
}  // namespace FnNs
int fnFree(int);

// C-6: the marks (Game/LumaMarks.hpp) on numbers, an enum and vectors (a plain one, a template's instance, one through a
// base). The unit's definitions are unmarked, but fnDefined's, which only its definition marks.
enum FnColor { FnRed, FnBlue };
struct FnVec { float x, y, z; };
template <class T>
struct FnV2 { T x, y; };
struct FnQuat { float x, y, z, w; };
struct FnVec4 : FnQuat {};
void fnOut(LUMA_OUT FnVec* out);
void fnIn(LUMA_IN const float* value);
void fnInout(LUMA_INOUT FnColor* color);
void fnKept(LUMA_KEPT int* registered);
void fnCount(LUMA_IN LUMA_COUNT(n) const float* p, int n, LUMA_OUT LUMA_NULLABLE FnVec* out);
void fnFixed(LUMA_OUT LUMA_COUNT_FIXED(12) float* m);
void fnInside(const LUMA_IN FnV2<float>* p, LUMA_INOUT FnVec4* q);  // a mark inside the parameter's own text
void fnDefined(FnVec* out);
struct FnMarked {
    void get(LUMA_OUT FnVec* out) const;  // a method: its `this` is not in the list
};
// A mark in a macro's argument is recorded where it is written, and binds.
#define FN_DECLARE(type) void fnByMacro(type v)
FN_DECLARE(LUMA_OUT float*);
