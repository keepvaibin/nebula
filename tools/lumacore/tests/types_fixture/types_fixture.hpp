#pragma once
// The type-rows fixture (contract types TY-8): what the game's headers have too little of to prove, each kind once.
// tools/lumacore/types_fixture_test.py runs gen_types.py on this folder alone, compiles its table with the kernel
// (LumaTypes.cpp) and a driver, and checks the kernel's answers and the build's comparison (compare_types.py).
#include <nebula_types.h>

// A name the record declares hides its bases' (FxDerived.x is its own); one two bases up is found (FxDerived.shared).
struct FxBase {
    s32 x;
    s32 shared;
};
struct FxMid : FxBase {
    s32 y;
};
struct FxDerived : FxMid {
    s32 z;
    f32 x;
};

// Two bases with one name: ambiguous, refused naming both.
struct FxA {
    s32 dup;
    s32 onlyA;
};
struct FxB {
    s32 dup;
};
struct FxBoth : FxA, FxB {
    s32 own;
};

// One base reached twice without `virtual`: ambiguous too.
struct FxTwiceL : FxA {};
struct FxTwiceR : FxA {};
struct FxTwice : FxTwiceL, FxTwiceR {};

// A base reached through `virtual`: its place depends on the most derived object, refused naming the base.
struct FxV {
    s32 v;
};
struct FxL : virtual FxV {
    s32 l;
};
struct FxR : virtual FxV {
    s32 r;
};
struct FxDiamond : FxL, FxR {
    s32 d;
};

// Bit-fields, their own and through a base that does not start the record.
struct FxBits {
    u32 a : 3;
    u32 b : 5;
    u16 c : 9;
    u8 after;
};
struct FxBitsChild : FxMid, FxBits {
    s32 tail;
};

// A template's instances: by the field's type, and by an alias only.
template <class T> struct FxBox {
    T value;
    T more[3];
    T grid[2][4];
};
typedef FxBox<float> FxBoxF;
typedef FxBox<s16> FxBoxS;
struct FxHolder {
    FxBox<float> box;
    FxBox<s32> ibox;
};

// Anonymous members: their fields are the owner's ("i"), a named one's "named.s".
struct FxAnon {
    union {
        s32 i;
        f32 f;
    };
    struct {
        s16 s;
    } named;
};

// TY-9: every name a text holds is a row, a built-in or left out: a foreign typedef (an alias row), an instance only
// a pointer names and one only a function pointer's parameter names (rows of their own), a pointer to an array.
struct FxNames {
    uintptr_t address;
    FxBox<u8>* boxes;
    FxBase (*rows)[2];
    FxMid& (*pick)(FxBase*, FxBox<u16>&);
};

// TY-10: a bit copy is the C++ copy (trivial_copy) and dropping one frees nothing (trivial_destroy).
struct FxOwns {  // its own destructor: neither
    s32* p;
    ~FxOwns() {}
};
struct FxCopies {  // its own copy: destructible only
    s32 v;
    FxCopies() : v(0) {}
    FxCopies(const FxCopies& o) : v(o.v) {}
};
struct FxDefaulted {  // defaulted ones are not its own: both
    s32 v;
    FxDefaulted() = default;
    FxDefaulted(const FxDefaulted&) = default;
    ~FxDefaulted() = default;
};
struct FxNoCopy {  // every copy and move deleted: clang calls it trivially copyable (no CWG1734), and no_copy
    s32 v;
    FxNoCopy(const FxNoCopy&) = delete;
    FxNoCopy& operator=(const FxNoCopy&) = delete;
};
struct FxHoldsNoCopy {  // its implicit copy is deleted by its field's: no_copy
    FxNoCopy held;
};
struct FxMoveOnly {  // a move to call: not no_copy
    s32 v;
    FxMoveOnly(FxMoveOnly&&) = default;
};
struct FxConstField {  // a const field: no assignment, a copy (clang lists its implicit members once a trait asks)
    s32 a;
    const s32 b;
};
static_assert(!__is_assignable(FxConstField&, FxConstField&&) && __is_constructible(FxConstField, const FxConstField&),
              "FxConstField: copied, never assigned");
struct FxNoDestroy {  // a deleted destructor: trivially copyable (clang), not destructible; assigned, never constructed
    s32 v;
    ~FxNoDestroy() = delete;
};
struct FxNoDestroyConst {  // and a const field: no_copy
    s32 v;
    const s32 c;
    ~FxNoDestroyConst() = delete;
};
struct FxHoldsNoDestroy {  // a part's deleted destructor deletes the implicit copy (B02-1), the const field the assignment
    FxNoDestroy held;
    const s32 c;
};
static_assert(!__is_constructible(FxNoDestroy, const FxNoDestroy&) && __is_assignable(FxNoDestroy&, const FxNoDestroy&),
              "FxNoDestroy: assigned, never constructed");
static_assert(!__is_constructible(FxNoDestroyConst, const FxNoDestroyConst&) &&
                  !__is_assignable(FxNoDestroyConst&, const FxNoDestroyConst&),
              "FxNoDestroyConst: never copied");
static_assert(!__is_constructible(FxHoldsNoDestroy, const FxHoldsNoDestroy&) &&
                  !__is_assignable(FxHoldsNoDestroy&, const FxHoldsNoDestroy&),
              "FxHoldsNoDestroy: never copied");
struct FxHoldsOwner {  // a field that frees: neither
    FxOwns owned;
};
struct FxPoly {  // a vtable: destructible only
    virtual void f() {}
};

// Contract objects (plan 1.3): an object's own class from its vtable, through primary and secondary bases.
struct FoA {
    virtual ~FoA() {}
    virtual s32 fa() { return 1; }
    virtual s32 add(s32 x, const s32& y) const { return x + y + a; }
    s32 a = 0;
};
struct FoB {
    virtual s32 fb() { return 2; }
    s32 b = 0;
};
struct FoC : FoA, FoB {  // FoB is a secondary base, at a non-zero offset
    s32 fa() override { return 3; }
    s32 fb() override { return 4; }
    s32 add(s32 x, const s32& y) const override { return x * y; }
    s32 c = 0;
};
struct FoD : FoC {
    s32 fb() override { return 5; }

private:
    virtual s32 secret(s32 x) const { return x * 7; }  // a private virtual: a method row all the same
};
struct FoV {  // polymorphic by its destructor alone: a type_info, no method row
    virtual ~FoV() {}
    s32 v = 0;
};
struct FoX : FoA {};
struct FoAmb : FoC, FoX {};  // two FoA subobjects: as FoA, ambiguous
// One class reached twice, with no virtual base: each subobject has its own final overrider (contract objects M-1).
struct FoTA {
    virtual s32 f() { return 10; }
    s32 ta = 0;
};
struct FoTB1 : FoTA {
    s32 f() override { return 11; }
};
struct FoTB2 : FoTA {
    s32 f() override { return 12; }
};
struct FoTD : FoTB1, FoTB2 {};
struct FoTE : FoV, FoTB1, FoTB2 {};  // at offset 0 a FoV: no FoTA there
// Polymorphic only through an instance named nowhere but as a base, which declares nothing virtual itself
// (the game's DeriveActorGroup<T> holders: a base walked after the record, or with no row of its own).
template <class T>
struct FoGroup : FoV {
    T* first = nullptr;
};
struct FoHolder : FoGroup<FoB> {
    s32 h = 0;
};

// Globals: one defined in the header (a row), one only declared (left out), one constant (a value, no row needed).
inline s32 gFxDefined = 7;
extern s32 gFxDeclaredOnly;
constexpr s32 kFxConstant = 42;
