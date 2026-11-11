// The type-rows fixture's driver (tools/lumacore/types_fixture_test.py): the kernel (LumaTypes.cpp) over the fixture's
// table, each answer against what the compiler says, then the kernel's description written for compare_types.py.
// Prints "bad <what>" for each wrong answer and "checks N bad M"; exit 0 only with none bad.
#include "Game/System/LumaTypes.hpp"
#include "types_fixture.hpp"
#include <luma.h>

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <typeinfo>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#pragma clang diagnostic ignored "-Winvalid-offsetof"

// What the kernel needs of the game module.
extern "C" void nebula_panic(const char* file, int line, const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::fprintf(stderr, "panic %s:%d: ", file, line);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::exit(3);
}
// The hook runner's ask marker (LumaHook.cpp): a method row's invoke answers its result's kind when given it.
namespace luma {
    void* kAskKind[1] = {nullptr};
}
extern "C" int nebula_vsnprintf(char* pDst, size_t length, const char* pFormat, va_list args) {
    return std::vsnprintf(pDst, length, pFormat, args);
}
extern "C" int nebula_snprintf(char* pDst, size_t length, const char* pFormat, ...) {
    va_list args;
    va_start(args, pFormat);
    const int n = std::vsnprintf(pDst, length, pFormat, args);
    va_end(args);
    return n;
}

s32 gFxDeclaredOnly;  // defined here, not in the header's unit: the table has no row for it (no .globals names it)

namespace {
    int sChecks = 0;
    int sBad = 0;

    void check(bool ok, const char* what) {
        sChecks++;
        if (!ok) {
            sBad++;
            std::printf("bad %s\n", what);
        }
    }

    void bound(const char* name, const char* type, size_t offset) {
        const luma::FieldBind b = luma::findField(name, type);
        char what[256];
        std::snprintf(what, sizeof(what), "%s (%s) at %u: found=%d same=%d value=%d why=%d", name, type,
                      static_cast<unsigned>(offset), b.found, b.sameType, static_cast<int>(b.value), static_cast<int>(b.why));
        check(b.found && b.sameType && b.value == static_cast<s32>(offset) && b.why == luma::FieldBind::kNone, what);
    }

    void refused(const char* name, int why, const char* names) {
        const luma::FieldBind b = luma::findField(name, nullptr);
        char what[384];
        std::snprintf(what, sizeof(what), "%s refused %d (%s): found=%d why=%d names=%s", name, why, names, b.found,
                      static_cast<int>(b.why), b.whyNames);
        check(!b.found && static_cast<int>(b.why) == why && std::strcmp(b.whyNames, names) == 0, what);
    }

    // A bit-field's position as the kernel binds it, proven by the compiler's own access: a value written through
    // the field reads back through LUMA_BITS_GET, and one set through LUMA_BITS_SET reads back through the field.
    template <class T, class Set, class Get> void bits(const char* name, const char* type, u32 width, Set set, Get get) {
        const luma::FieldBind b = luma::findField(name, type);
        char what[256];
        std::snprintf(what, sizeof(what), "%s (%s): found=%d same=%d", name, type, b.found, b.sameType);
        check(b.found && b.sameType, what);
        if (!b.found) {
            return;
        }
        const u32 most = width >= 32 ? 0xFFFFFFFFu : (1u << width) - 1u;
        T object;
        std::memset(&object, 0, sizeof(object));
        set(object, most & 0x15u);
        std::snprintf(what, sizeof(what), "%s at bit %d: read back", name, static_cast<int>(b.value));
        check(LUMA_BITS_GET(&object, b.value, width) == (most & 0x15u), what);
        std::memset(&object, 0xFF, sizeof(object));
        LUMA_BITS_SET(&object, b.value, width, 0);
        std::snprintf(what, sizeof(what), "%s at bit %d: set", name, static_cast<int>(b.value));
        check(get(object) == 0, what);
    }

    // TY-4: the bit accessors touch only the bytes the field covers. A field in the last byte of a page whose next
    // page has no access: any wider access faults, and the test dies.
    void lastByte() {
#if defined(_WIN32)
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        const size_t page = info.dwPageSize;
        unsigned char* const pages = static_cast<unsigned char*>(VirtualAlloc(nullptr, page * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        DWORD was = 0;
        const bool guarded = pages != nullptr && VirtualProtect(pages + page, page, PAGE_NOACCESS, &was) != 0;
#else
        const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
        void* const mapped = mmap(nullptr, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        unsigned char* const pages = mapped == MAP_FAILED ? nullptr : static_cast<unsigned char*>(mapped);
        const bool guarded = pages != nullptr && mprotect(pages + page, page, PROT_NONE) == 0;
#endif
        check(guarded, "the guard page");
        if (!guarded) {
            return;
        }
        unsigned char* const last = pages + page - 1;
        *last = 0xA5;  // 1010 0101: bits 3 to 7 are 10100
        check(LUMA_BITS_GET(last, 3, 5) == 0x14u, "last byte: get");
        LUMA_BITS_SET(last, 3, 5, 0x0Bu);
        check(*last == static_cast<unsigned char>((0x0Bu << 3) | 0x05u), "last byte: set keeps the other bits");
    }

    void put(void* user, const char* text, uint32_t length) {
        std::fwrite(text, 1, length, static_cast<std::FILE*>(user));
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: types_fixture_driver <describe.json>\n");
        return 2;
    }
    luma::prepareTypes();
    // TY-1: own, hidden, two bases up.
    bound("FxDerived.x", "f32", offsetof(FxDerived, x));
    bound("FxDerived.shared", "s32", offsetof(FxDerived, shared));
    bound("FxDerived.y", "s32", offsetof(FxDerived, y));
    bound("FxBoth.onlyA", "s32", offsetof(FxBoth, onlyA));
    bound("FxBoth.own", "s32", offsetof(FxBoth, own));
    {
        const luma::FieldBind b = luma::findField("FxDerived.x", "s32");
        check(b.found && !b.sameType, "FxDerived.x with its base's type: found, another type (Z-4)");
    }
    // TY2-1: ambiguous and through a virtual base, with their names.
    refused("FxBoth.dup", luma::FieldBind::kAmbiguous, "FxA::dup, FxB::dup");
    refused("FxTwice.dup", luma::FieldBind::kAmbiguous, "FxA::dup, FxA::dup");
    refused("FxDiamond.v", luma::FieldBind::kThroughVirtualBase, "FxV");
    refused("FxL.v", luma::FieldBind::kThroughVirtualBase, "FxV");
    bound("FxDiamond.d", "s32", offsetof(FxDiamond, d));
    // No such name: not found, no reason.
    refused("FxDerived.nothing", luma::FieldBind::kNone, "");
    refused("FxNoSuchType.x", luma::FieldBind::kNone, "");
    refused("FxDerived.", luma::FieldBind::kNone, "");
    // Bit-fields: their own, and through a base that does not start the record.
    bits<FxBits>("FxBits.a", "u32 : 3", 3, [](FxBits& o, u32 v) { o.a = v; }, [](const FxBits& o) { return static_cast<u32>(o.a); });
    bits<FxBits>("FxBits.b", "u32 : 5", 5, [](FxBits& o, u32 v) { o.b = v; }, [](const FxBits& o) { return static_cast<u32>(o.b); });
    bits<FxBits>("FxBits.c", "u16 : 9", 9, [](FxBits& o, u32 v) { o.c = static_cast<u16>(v); },
                 [](const FxBits& o) { return static_cast<u32>(o.c); });
    bits<FxBitsChild>("FxBitsChild.b", "u32 : 5", 5, [](FxBitsChild& o, u32 v) { o.b = v; },
                      [](const FxBitsChild& o) { return static_cast<u32>(o.b); });
    bound("FxBitsChild.after", "u8", offsetof(FxBitsChild, after));
    check(luma::findField("FxBits.b", "u32").found && !luma::findField("FxBits.b", "u32").sameType,
          "a bit-field named with its plain type is another type (Z-4)");
    // TY-2: an instance by its row's name and by its aliases; one only an alias names has a size. FxBox<f32> and
    // FxBox<float> are one record, so a field of type T reads as the argument itself (float), true for both spellings.
    bound("FxBox<f32>.more", "float", offsetof(FxBox<float>, more));
    bound("FxBoxF.grid", "float", offsetof(FxBox<float>, grid));
    bound("FxBox<float>.value", "float", offsetof(FxBox<float>, value));
    check(!luma::findField("FxBox<f32>.value", "f32").sameType, "FxBox<f32>.value written f32 is another text (Z-4)");
    bound("FxHolder.ibox", "FxBox<s32>", offsetof(FxHolder, ibox));
    {
        const luma::TypeRow* const pS = luma::findType("FxBoxS");
        check(pS != nullptr && pS->size == sizeof(FxBox<s16>) && pS->align == alignof(FxBox<s16>), "FxBoxS's size by its alias");
        const luma::TypeRow* const pBox = luma::findType("FxBox<f32>");
        bool counts = false;
        for (unsigned i = 0; pBox != nullptr && i < pBox->fieldCount; i++) {
            if (std::strcmp(pBox->fields[i].name, "grid") == 0) {
                counts = pBox->fields[i].count == 8;
            }
        }
        check(counts, "FxBox<f32>.grid counts 8 elements");
        check(luma::findType("FxBox") == nullptr, "a template by its own name has no row");
    }
    // Anonymous members.
    bound("FxAnon.i", "s32", offsetof(FxAnon, i));
    bound("FxAnon.f", "f32", offsetof(FxAnon, f));
    bound("FxAnon.named.s", "s16", offsetof(FxAnon, named) + offsetof(decltype(FxAnon::named), s));
    refused("FxAnon.named", luma::FieldBind::kNone, "");
    // TY-9: the names a text holds.
    bound("FxNames.address", "uintptr_t", offsetof(FxNames, address));
    bound("FxNames.boxes", "FxBox<u8> *", offsetof(FxNames, boxes));
    bound("FxNames.rows", "FxBase (*)[2]", offsetof(FxNames, rows));
    bound("FxNames.pick", "FxMid &(*)(FxBase *, FxBox<u16> &)", offsetof(FxNames, pick));
    {
        const luma::TypeRow* const pU8 = luma::findType("FxBox<u8>");
        check(pU8 != nullptr && pU8->size == sizeof(FxBox<u8>), "FxBox<u8>, named only through a pointer, has its row");
        const luma::TypeRow* const pU16 = luma::findType("FxBox<u16>");
        check(pU16 != nullptr && pU16->size == sizeof(FxBox<u16>), "FxBox<u16>, named only as a parameter, has its row");
    }
    // Globals: a header's definition has its address; one no unit of the table defines has none.
    {
        const luma::GlobalRow* const pDefined = luma::findGlobal("gFxDefined");
        check(pDefined != nullptr && pDefined->address == &gFxDefined && std::strcmp(pDefined->type, "s32") == 0,
              "gFxDefined: its address and type");
        check(luma::findGlobal("gFxDeclaredOnly") == nullptr, "gFxDeclaredOnly: no row");
        check(luma::findGlobal("kFxConstant") == nullptr, "kFxConstant: no row (internal linkage)");
    }
    // TY-7: the generator's left rows, found as the validator reads left_out (the loader's L505 why and what).
    {
        struct Want {
            u32 kind;
            const char* name;
            const char* why;   // null: no left row
            const char* what;  // null: none
        };
        const Want wants[] = {
            {LUMA_IMPORT_GLOBAL, "gFxDeclaredOnly", "LeftOut.NotDefined", nullptr},
            {LUMA_IMPORT_SIZE, "FxBox<s8>", "LeftOut.TemplateUnused", nullptr},
            {LUMA_IMPORT_FIELD, "FxBox<s8>.value", "LeftOut.TemplateUnused", nullptr},
            {LUMA_IMPORT_FIELD, "FxBoth.dup", "LeftOut.Ambiguous", "FxA::dup, FxB::dup"},
            {LUMA_IMPORT_FIELD, "FxL.v", "LeftOut.VirtualBase", "FxV"},
            {LUMA_IMPORT_FIELD, "FxDerived.nothing", nullptr, nullptr},
            {LUMA_IMPORT_GLOBAL, "gFxDefined", nullptr, nullptr},
            {LUMA_IMPORT_SIZE, "FxNoSuchType", nullptr, nullptr},
        };
        for (const Want& w : wants) {
            const luma::LeftRow* const pRow = luma::leftOut(w.kind, w.name);
            char what[384];
            std::snprintf(what, sizeof(what), "left row of %s: %s %s, want %s %s", w.name, pRow ? pRow->why : "(none)",
                          pRow && pRow->what ? pRow->what : "", w.why ? w.why : "(none)", w.what ? w.what : "");
            const bool ok = w.why == nullptr ? pRow == nullptr
                                             : pRow != nullptr && std::strcmp(pRow->why, w.why) == 0 &&
                                                   (w.what == nullptr ? pRow->what == nullptr
                                                                      : pRow->what != nullptr && std::strcmp(pRow->what, w.what) == 0);
            check(ok, what);
        }
    }
    // Contract objects: the own class and the subobjects, against the compiler's typeid and dynamic_cast (this driver
    // has the C++ runtime; the module never calls it).
    {
        struct DriverOnly : FoD {};  // a class with no row: the kernel names its type_info, no row
        static FoD d;
        static FoC c;
        static FoAmb amb;
        static DriverOnly only;
        const luma::TypeRow* const pA = luma::findType("FoA");
        const luma::TypeRow* const pB = luma::findType("FoB");
        const luma::TypeRow* const pC = luma::findType("FoC");
        const luma::TypeRow* const pD = luma::findType("FoD");
        check(pA && pB && pC && pD && pA->typeInfo == &typeid(FoA) && pD->typeInfo == &typeid(FoD),
              "FoA to FoD have rows with their type_info");
        check(luma::findType("FxBase") != nullptr && luma::findType("FxBase")->typeInfo == nullptr,
              "a record with no vtable has no type_info");
        struct Case {
            const void* object;            // as some polymorphic base
            const void* complete;          // the whole object: dynamic_cast<const void*>
            const std::type_info* own;     // the compiler's typeid of the object, through that base
            const luma::TypeRow* row;
            const char* what;
        };
        const Case cases[] = {
            {static_cast<FoA*>(&d), dynamic_cast<const void*>(static_cast<FoA*>(&d)), &typeid(*static_cast<FoA*>(&d)), pD,
             "FoD seen as FoA"},
            {static_cast<FoB*>(&d), dynamic_cast<const void*>(static_cast<FoB*>(&d)), &typeid(*static_cast<FoB*>(&d)), pD,
             "FoD seen as FoB (a secondary base)"},
            {static_cast<FoB*>(&c), dynamic_cast<const void*>(static_cast<FoB*>(&c)), &typeid(*static_cast<FoB*>(&c)), pC,
             "FoC seen as FoB"},
            {static_cast<FoA*>(&c), dynamic_cast<const void*>(static_cast<FoA*>(&c)), &typeid(*static_cast<FoA*>(&c)), pC,
             "FoC seen as FoA"},
            {static_cast<FoB*>(&only), dynamic_cast<const void*>(static_cast<FoB*>(&only)), &typeid(*static_cast<FoB*>(&only)),
             nullptr, "a class with no row, seen as FoB"},
        };
        check(cases[1].own == &typeid(FoD) && cases[1].complete == &d && cases[4].own == &typeid(DriverOnly),
              "the compiler's own answers are the cases' (FoD's, DriverOnly's)");
        for (const Case& k : cases) {
            const luma::OwnClass own = luma::ownClass(k.object);
            char what[256];
            std::snprintf(what, sizeof(what), "ownClass of %s: row %s, type_info %s, whole object %s", k.what,
                          own.type ? own.type->name : "(none)", own.typeInfo == k.own ? "right" : "wrong",
                          own.complete == k.complete ? "right" : "wrong");
            check(own.type == k.row && own.typeInfo == k.own && own.complete == k.complete, what);
        }
        bool ambiguous = true;
        const void* p = luma::asClass(static_cast<FoB*>(&d), pA, &ambiguous);
        check(p == dynamic_cast<FoA*>(static_cast<FoB*>(&d)) && !ambiguous, "asClass FoD from FoB to FoA is dynamic_cast's");
        p = luma::asClass(static_cast<FoA*>(&d), pB, &ambiguous);
        check(p == dynamic_cast<FoB*>(static_cast<FoA*>(&d)) && !ambiguous, "asClass FoD from FoA to FoB is dynamic_cast's");
        p = luma::asClass(static_cast<FoA*>(&d), pD, &ambiguous);
        check(p == &d && !ambiguous, "asClass FoD to its own class");
        p = luma::asClass(static_cast<FoA*>(&c), pD, &ambiguous);
        check(p == nullptr && !ambiguous && dynamic_cast<FoD*>(static_cast<FoA*>(&c)) == nullptr,
              "asClass FoC to FoD is null, as dynamic_cast's");
        p = luma::asClass(static_cast<FoB*>(&amb), pA, &ambiguous);
        check(p == nullptr && ambiguous && dynamic_cast<FoA*>(static_cast<FoB*>(&amb)) == nullptr,
              "asClass FoAmb to FoA is ambiguous, null as dynamic_cast's");
        p = luma::asClass(static_cast<FoB*>(&amb), pC, &ambiguous);
        check(p == static_cast<FoC*>(&amb) && !ambiguous, "asClass FoAmb to FoC");
        p = luma::asClass(static_cast<FoB*>(&only), pD, &ambiguous);
        check(p == nullptr && !ambiguous, "asClass of a class with no row is null");
        check(luma::ownClass(nullptr).type == nullptr && luma::ownClass(nullptr).typeInfo == nullptr, "ownClass of null");
        // Contract objects S-1: a pointer not aligned as an object's, and an object whose first word is no vtable
        // (a plain struct's data, a misaligned word), are no class: none, nothing read past them.
        {
            alignas(8) static unsigned char bytes[32] = {};
            check(luma::ownClass(bytes + 1).type == nullptr, "ownClass of a misaligned pointer");
            alignas(8) static __UINTPTR_TYPE__ notVtable[2] = {3, 0};
            check(luma::ownClass(notVtable).type == nullptr, "ownClass of an object whose first word is misaligned");
        }
        check(luma::findType("FoV") && luma::findType("FoV")->typeInfo == &typeid(FoV),
              "a record whose only virtual is its destructor is polymorphic: its type_info");

        // Virtual calls: the thunk dispatches by the object's own class, as the compiler's own call does.
        struct Call {
            const char* name;
            const char* signature;
            void* object;  // as the declaring class
            s32 x, y;
            s32 want;
        };
        const Call calls[] = {
            {"FoA::fa", "s32()", static_cast<FoA*>(&d), 0, 0, static_cast<FoA*>(&d)->fa()},
            {"FoB::fb", "s32()", static_cast<FoB*>(&d), 0, 0, static_cast<FoB*>(&d)->fb()},
            {"FoB::fb", "s32()", static_cast<FoB*>(&c), 0, 0, static_cast<FoB*>(&c)->fb()},
            {"FoA::add", "s32(s32, const s32 &) const", static_cast<FoA*>(&d), 6, 7, 42},
            {"FoD::secret", "s32(s32) const", &d, 3, 0, 21},
        };
        check(calls[0].want == 3 && calls[1].want == 5 && calls[2].want == 4, "the compiler's calls are the cases'");
        for (const Call& k : calls) {
            const luma::Function* const pM = luma::findMethod(k.name, k.signature);
            s32 x = k.x;
            s32 y = k.y;
            void* args[] = {k.object, &x, &y};
            s32 got = -1;
            unsigned kind = 99;
            if (pM != nullptr) {
                pM->invoke(args, &got);
                pM->invoke(luma::kAskKind, &kind);
            }
            char what[256];
            std::snprintf(what, sizeof(what), "method %s %s: %s, %d (want %d), kind %u, hooked %s", k.name, k.signature,
                          pM ? "found" : "none", static_cast<int>(got), static_cast<int>(k.want), kind,
                          pM && pM->hooked == nullptr ? "null" : "set");
            check(pM != nullptr && got == k.want && kind == luma::kPlain && pM->hooked == nullptr, what);
        }
        // The overrider: the declaration the own class runs, as the calls above ran it.
        struct Over {
            const void* object;
            const char* name;
            const char* signature;
            const char* want;  // the overrider's function name; null: none
        };
        static FoX x;
        // Contract objects M-1: FoTA reached twice; each subobject's overrider is its own path's, as the compiler
        // calls it; from a pointer that is no FoTA (FoTE's FoV at offset 0), none, said as ambiguous.
        static FoTD td;
        static FoTE te;
        FoTA* const pTd1 = static_cast<FoTB1*>(&td);
        FoTA* const pTd2 = static_cast<FoTB2*>(&td);
        FoTA* const pTe2 = static_cast<FoTB2*>(&te);
        check(pTd1->f() == 11 && pTd2->f() == 12 && pTe2->f() == 12, "the compiler's calls on FoTA's two subobjects");
        {
            bool ambiguous = true;
            const luma::MethodRow* pRow = luma::overrider(pTd2, "FoTA::f", "s32()", &ambiguous);
            check(pRow != nullptr && std::strcmp(pRow->function->name, "FoTB2::f") == 0 && !ambiguous, "overrider FoTD's second FoTA: FoTB2::f");
            pRow = luma::overrider(pTd1, "FoTA::f", "s32()", &ambiguous);
            check(pRow != nullptr && std::strcmp(pRow->function->name, "FoTB1::f") == 0 && !ambiguous, "overrider FoTD's first FoTA: FoTB1::f");
            pRow = luma::overrider(pTe2, "FoTA::f", "s32()", &ambiguous);
            check(pRow != nullptr && std::strcmp(pRow->function->name, "FoTB2::f") == 0 && !ambiguous, "overrider FoTE's second FoTA: FoTB2::f");
            pRow = luma::overrider(static_cast<FoV*>(&te), "FoTA::f", "s32()", &ambiguous);
            check(pRow == nullptr && ambiguous, "overrider from FoTE's FoV: none, ambiguous");
            const luma::Function* const pM = luma::findMethod("FoTA::f", "s32()");
            s32 got = -1;
            void* args[] = {pTd2};
            if (pM != nullptr) {
                pM->invoke(args, &got);
            }
            check(got == 12, "method FoTA::f on FoTD's second FoTA runs FoTB2::f");
        }
        const Over overs[] = {
            {static_cast<FoA*>(&d), "FoA::fa", "s32()", "FoC::fa"},
            {static_cast<FoB*>(&d), "FoB::fb", "s32()", "FoD::fb"},
            {static_cast<FoB*>(&c), "FoB::fb", "s32()", "FoC::fb"},
            {static_cast<FoA*>(&d), "FoA::add", "s32(s32, const s32 &) const", "FoC::add"},
            {static_cast<FoA*>(&x), "FoA::fa", "s32()", "FoA::fa"},
            {&d, "FoD::secret", "s32(s32) const", "FoD::secret"},
            {static_cast<FoB*>(&only), "FoB::fb", "s32()", nullptr},  // a class with no row
            {static_cast<FoA*>(&d), "FoA::fa", "s32(s32)", nullptr},  // no such virtual
        };
        for (const Over& k : overs) {
            bool ambiguous = true;
            const luma::MethodRow* const pRow = luma::overrider(k.object, k.name, k.signature, &ambiguous);
            check(!ambiguous, "overrider says nothing ambiguous with one subobject");
            const char* const got = pRow != nullptr ? pRow->function->name : nullptr;
            char what[256];
            std::snprintf(what, sizeof(what), "overrider %s %s: %s (want %s)", k.name, k.signature, got ? got : "none",
                          k.want ? k.want : "none");
            check(k.want == nullptr ? got == nullptr : got != nullptr && std::strcmp(got, k.want) == 0, what);
        }
        check(luma::findMethod("FoA::fa", "s32(s32)") == nullptr && luma::findMethod("FoA::nope", "s32()") == nullptr &&
                  luma::findMethod("FoA::~FoA", "()") == nullptr,
              "no method row for another signature, an unknown name or a destructor");
    }
    lastByte();
    std::FILE* const file = std::fopen(argv[1], "wb");
    check(file != nullptr, "the describe file opens");
    if (file != nullptr) {
        luma::describeTypes(put, file);
        check(std::fclose(file) == 0, "the describe file is written");
    }
    std::printf("checks %d bad %d\n", sChecks, sBad);
    return sBad != 0 ? 1 : 0;
}
