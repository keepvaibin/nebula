// The function facts' fixture (tools/lumacore/functions_fixture_test.py, the checker design's C-1): each kind of row,
// virtual, pure and const, and overrides through one base, two bases, a destructor and a template's instance.
#pragma once

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
