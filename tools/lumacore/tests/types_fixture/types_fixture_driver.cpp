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
