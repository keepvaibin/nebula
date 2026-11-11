// The type and global rows the linker gathered (luma_types.h; contract types, plan 1.2), found by name when a mod binds.
// Nothing runs at start: prepareTypes sorts them once, on the game thread, when the first mod loads.
#include "Game/System/LumaTypes.hpp"

#include <luma.h>
#include <nebula_format.h>
#include <nebula_game_api.h>
#include <nebula_module.h>
#include <stdlib.h>

namespace luma {
#if defined(__APPLE__)
    extern const TypeRow sTypesStart __asm("section$start$__DATA$lumaty");
    extern const TypeRow sTypesStop __asm("section$end$__DATA$lumaty");
    extern const AliasRow sAliasesStart __asm("section$start$__DATA$lumaal");
    extern const AliasRow sAliasesStop __asm("section$end$__DATA$lumaal");
    extern const GlobalRow sGlobalsStart __asm("section$start$__DATA$lumagl");
    extern const GlobalRow sGlobalsStop __asm("section$end$__DATA$lumagl");
    extern const LeftRow sLeftStart __asm("section$start$__DATA$lumalo");
    extern const LeftRow sLeftStop __asm("section$end$__DATA$lumalo");
#elif !defined(_WIN32)
    // Weak: a module with no rows of a kind has no such section, and both bounds are then null.
    extern "C" __attribute__((weak)) const TypeRow __start_lumaty[];
    extern "C" __attribute__((weak)) const TypeRow __stop_lumaty[];
    extern "C" __attribute__((weak)) const AliasRow __start_lumaal[];
    extern "C" __attribute__((weak)) const AliasRow __stop_lumaal[];
    extern "C" __attribute__((weak)) const GlobalRow __start_lumagl[];
    extern "C" __attribute__((weak)) const GlobalRow __stop_lumagl[];
    extern "C" __attribute__((weak)) const LeftRow __start_lumalo[];
    extern "C" __attribute__((weak)) const LeftRow __stop_lumalo[];
#endif

    namespace {
#if defined(_WIN32)
        __attribute__((section("lumaty$a"), used)) const TypeRow sTypesBefore = {};
        __attribute__((section("lumaty$z"), used)) const TypeRow sTypesAfter = {};
        __attribute__((section("lumaal$a"), used)) const AliasRow sAliasesBefore = {};
        __attribute__((section("lumaal$z"), used)) const AliasRow sAliasesAfter = {};
        __attribute__((section("lumagl$a"), used)) const GlobalRow sGlobalsBefore = {};
        __attribute__((section("lumagl$z"), used)) const GlobalRow sGlobalsAfter = {};
        const TypeRow* typesBegin() { return &sTypesBefore + 1; }
        const TypeRow* typesEnd() { return &sTypesAfter; }
        const AliasRow* aliasesBegin() { return &sAliasesBefore + 1; }
        const AliasRow* aliasesEnd() { return &sAliasesAfter; }
        const GlobalRow* globalsBegin() { return &sGlobalsBefore + 1; }
        const GlobalRow* globalsEnd() { return &sGlobalsAfter; }
        __attribute__((section("lumalo$a"), used)) const LeftRow sLeftBefore = {};
        __attribute__((section("lumalo$z"), used)) const LeftRow sLeftAfter = {};
        const LeftRow* leftBegin() { return &sLeftBefore + 1; }
        const LeftRow* leftEnd() { return &sLeftAfter; }
#elif defined(__APPLE__)
        const TypeRow* typesBegin() { return &sTypesStart; }
        const TypeRow* typesEnd() { return &sTypesStop; }
        const AliasRow* aliasesBegin() { return &sAliasesStart; }
        const AliasRow* aliasesEnd() { return &sAliasesStop; }
        const GlobalRow* globalsBegin() { return &sGlobalsStart; }
        const GlobalRow* globalsEnd() { return &sGlobalsStop; }
        const LeftRow* leftBegin() { return &sLeftStart; }
        const LeftRow* leftEnd() { return &sLeftStop; }
#else
        const TypeRow* typesBegin() { return __start_lumaty; }
        const TypeRow* typesEnd() { return __stop_lumaty; }
        const AliasRow* aliasesBegin() { return __start_lumaal; }
        const AliasRow* aliasesEnd() { return __stop_lumaal; }
        const GlobalRow* globalsBegin() { return __start_lumagl; }
        const GlobalRow* globalsEnd() { return __stop_lumagl; }
        const LeftRow* leftBegin() { return __start_lumalo; }
        const LeftRow* leftEnd() { return __stop_lumalo; }
#endif

        // The first `length` bytes of pA against the whole of pB.
        s32 compare(const char* pA, u32 length, const char* pB) {
            for (u32 i = 0; i < length; i++) {
                if (pB[i] == 0 || pA[i] != pB[i]) {
                    return pB[i] == 0 ? 1 : static_cast<s32>(static_cast<unsigned char>(pA[i])) - static_cast<s32>(static_cast<unsigned char>(pB[i]));
                }
            }
            return pB[length] == 0 ? 0 : -1;
        }

        bool sameText(const char* pA, const char* pB) {
            while (*pA != 0 && *pA == *pB) {
                pA++;
                pB++;
            }
            return *pA == *pB;
        }

        u32 textLength(const char* p) {
            u32 n = 0;
            while (p[n] != 0) {
                n++;
            }
            return n;
        }

        // Which rows an index takes: all of a kind, or the left rows of one group.
        bool takes(const TypeRow*, u32) { return true; }
        bool takes(const AliasRow*, u32) { return true; }
        bool takes(const GlobalRow*, u32) { return true; }
        bool takes(const LeftRow* pRow, u32 group) { return pRow->group == group; }

        // One sorted index of rows of type T, by name; built by prepareTypes.
        template <class T> struct Index {
            const T** rows = nullptr;
            u32 count = 0;
            bool built = false;

            static bool before(const T* pA, const T* pB) { return compare(pA->name, textLength(pA->name), pB->name) < 0; }

            void sift(u32 root, u32 n) {
                for (;;) {
                    u32 child = root * 2 + 1;
                    if (child >= n) {
                        return;
                    }
                    if (child + 1 < n && before(rows[child], rows[child + 1])) {
                        child++;
                    }
                    if (!before(rows[root], rows[child])) {
                        return;
                    }
                    const T* const pHeld = rows[root];
                    rows[root] = rows[child];
                    rows[child] = pHeld;
                    root = child;
                }
            }

            void build(const T* pBegin, const T* pEnd, u32 group = 0) {
                built = true;
                rows = static_cast<const T**>(::malloc(sizeof(const T*) * static_cast<size_t>(pEnd - pBegin + 1)));
                if (rows == nullptr) {
                    NEBULA_PANIC("LumaCore: no memory to index the type rows");
                }
                for (const T* pRow = pBegin; pRow != pEnd; pRow++) {
                    if (pRow->name != nullptr && takes(pRow, group)) {  // a linker may pad between the files' rows
                        rows[count++] = pRow;
                    }
                }
                for (u32 i = count / 2; i-- > 0;) {
                    sift(i, count);
                }
                for (u32 end = count; end-- > 1;) {
                    const T* const pHeld = rows[0];
                    rows[0] = rows[end];
                    rows[end] = pHeld;
                    sift(0, end);
                }
                // Two rows of one name are the generator's fault: the build is wrong, and a mod must not guess.
                for (u32 i = 1; i < count; i++) {
                    if (sameText(rows[i - 1]->name, rows[i]->name)) {
                        NEBULA_PANIC("LumaCore: two rows are named %s", rows[i]->name);
                    }
                }
            }

            const T* find(const char* pName, u32 length) const {
                u32 low = 0;
                u32 high = count;
                while (low < high) {
                    const u32 mid = low + (high - low) / 2;
                    const s32 c = compare(pName, length, rows[mid]->name);
                    if (c == 0) {
                        return rows[mid];
                    }
                    if (c < 0) {
                        high = mid;
                    } else {
                        low = mid + 1;
                    }
                }
                return nullptr;
            }
        };

        Index<TypeRow> sTypes;
        Index<AliasRow> sAliases;
        Index<GlobalRow> sGlobals;
        Index<LeftRow> sLeft[kLeftGroups];

        // Where "Type.field"'s type name ends: at the first '.' outside a template's arguments (the field may hold
        // more, "mFlag.mIsDead"). The name's length when it has no such '.'.
        u32 typeEnd(const char* pName) {
            u32 depth = 0;
            u32 dot = 0;
            for (; pName[dot] != 0; dot++) {
                if (pName[dot] == '<') {
                    depth++;
                } else if (pName[dot] == '>' && depth != 0) {
                    depth--;
                } else if (pName[dot] == '.' && depth == 0) {
                    break;
                }
            }
            return dot;
        }

        // A type's reason, as the validator looks: the types group (a record, alias or enum), then the templates by
        // the name before its arguments ("TBox<s8>" as "TBox").
        const LeftRow* typeLeftOut(const char* pName, u32 length) {
            if (const LeftRow* const pRow = sLeft[kLeftType].find(pName, length)) {
                return pRow;
            }
            u32 plain = 0;
            while (plain < length && pName[plain] != '<') {
                plain++;
            }
            return sLeft[kLeftTemplate].find(pName, plain);
        }

        // A record by its row's name, else by an alias's (TY-2: "TVec3f" and "JGeometry::TVec3<float>" name the row
        // "JGeometry::TVec3<f32>"). An alias names its record directly: there is no chain to follow.
        const TypeRow* typeNamed(const char* pName, u32 length) {
            prepareTypes();
            if (const TypeRow* const pRow = sTypes.find(pName, length)) {
                return pRow;
            }
            const AliasRow* const pAlias = sAliases.find(pName, length);
            return pAlias != nullptr ? sTypes.find(pAlias->record, textLength(pAlias->record)) : nullptr;
        }

        // C++'s lookup of a field name (types.md TY-1, the rule gen_types.py's `reach` writes as `reachable`): a name
        // a record declares hides its bases'; otherwise each base subobject that has it is one finding. Bound only when
        // exactly one subobject has it and the path to it crosses no virtual base (a virtual base's place depends on
        // the most derived object).
        const u32 kMostFindings = 8;
        struct Lookup {
            const TypeField* pField;               // the first finding
            u32 offset;                            // its offset from the start of the record looked in
            u32 count;                             // subobjects that have the name
            const char* pVirtual;                  // the first finding's nearest virtual base on the way, else null
            const char* pFrom[kMostFindings];      // the records that declare each finding (the first kMostFindings)
            bool tooDeep;                          // a base chain deeper than 64: not bound
        };

        void lookIn(const TypeRow* pType, const char* pField, u32 base, const char* pVirtual, Lookup* pOut, u32 depth) {
            for (u32 i = 0; i < pType->fieldCount; i++) {
                if (sameText(pType->fields[i].name, pField)) {
                    if (pOut->count == 0) {
                        pOut->pField = &pType->fields[i];
                        pOut->offset = base + pType->fields[i].offset;
                    }
                    if (pOut->pVirtual == nullptr) {
                        pOut->pVirtual = pVirtual;
                    }
                    if (pOut->count < kMostFindings) {
                        pOut->pFrom[pOut->count] = pType->name;
                    }
                    pOut->count++;
                    return;  // hides the bases' fields of that name
                }
            }
            if (depth == 64) {
                pOut->tooDeep = true;
                return;
            }
            for (u32 i = 0; i < pType->baseCount; i++) {
                const TypeBase& rBase = pType->bases[i];
                const TypeRow* const pBase = typeNamed(rBase.name, textLength(rBase.name));
                if (pBase != nullptr) {
                    // The nearest virtual base wins, as the generator's reason names it.
                    lookIn(pBase, pField, base + rBase.offset, rBase.isVirtual != 0 ? rBase.name : pVirtual, pOut, depth + 1);
                }
            }
        }

        // Byte i of the joined name "<record>::<field>", 0 past its end.
        unsigned char joinedAt(const char* pRecord, u32 length, const char* pField, u32 lengthField, u32 i) {
            if (i < length) {
                return static_cast<unsigned char>(pRecord[i]);
            }
            if (i < length + 2) {
                return ':';
            }
            return i - (length + 2) < lengthField ? static_cast<unsigned char>(pField[i - (length + 2)]) : 0;
        }

        // "A::x" before "B::x" by bytes, as the generator sorts the joined names.
        bool joinedBefore(const char* pA, const char* pB, const char* pField) {
            const u32 lengthA = textLength(pA);
            const u32 lengthB = textLength(pB);
            const u32 lengthField = textLength(pField);
            const u32 most = (lengthA > lengthB ? lengthA : lengthB) + 2 + lengthField;
            for (u32 i = 0; i <= most; i++) {
                const unsigned char a = joinedAt(pA, lengthA, pField, lengthField, i);
                const unsigned char b = joinedAt(pB, lengthB, pField, lengthField, i);
                if (a != b) {
                    return a < b;
                }
            }
            return false;
        }

        // The describe output: JSON text in pieces of up to 4 KB to the host's writer.
        struct Out {
            NebulaDescribeWrite write;
            void* user;
            char buffer[4096];
            u32 used;

            void flush() {
                if (used != 0) {
                    write(user, buffer, used);
                    used = 0;
                }
            }
            void raw(const char* pText) {
                for (; *pText != 0; pText++) {
                    if (used == sizeof(buffer)) {
                        flush();
                    }
                    buffer[used++] = *pText;
                }
            }
            // A JSON string: quotes, backslashes and control bytes escaped; other bytes as they are (UTF-8).
            void text(const char* pText) {
                raw("\"");
                for (; *pText != 0; pText++) {
                    const unsigned char c = static_cast<unsigned char>(*pText);
                    char piece[8] = {static_cast<char>(c), 0};
                    if (c == '"' || c == '\\') {
                        piece[0] = '\\';
                        piece[1] = static_cast<char>(c);
                        piece[2] = 0;
                    } else if (c < 0x20) {
                        nebula_snprintf(piece, sizeof(piece), "\\u%04x", c);
                    }
                    raw(piece);
                }
                raw("\"");
            }
            void number(s64 value) {
                char piece[24];
                nebula_snprintf(piece, sizeof(piece), "%lld", static_cast<long long>(value));
                raw(piece);
            }
        };

        // Every name a record's bases give it, own names left out, each once, nearest first (the order of a walk).
        struct Names {
            const char** names = nullptr;
            u32 count = 0;
            u32 room = 0;

            void add(const char* pName) {
                for (u32 i = 0; i < count; i++) {
                    if (sameText(names[i], pName)) {
                        return;
                    }
                }
                if (count == room) {
                    room = room != 0 ? room * 2 : 64;
                    const char** const pMore = static_cast<const char**>(::realloc(names, sizeof(const char*) * room));
                    if (pMore == nullptr) {
                        NEBULA_PANIC("LumaCore: no memory to describe the type rows");
                    }
                    names = pMore;
                }
                names[count++] = pName;
            }
        };

        void gather(const TypeRow* pType, Names* pNames, u32 depth) {
            if (depth == 64) {
                return;  // lookIn refuses such a name too (tooDeep), and says nothing for it
            }
            for (u32 i = 0; i < pType->baseCount; i++) {
                const TypeRow* const pBase = typeNamed(pType->bases[i].name, textLength(pType->bases[i].name));
                if (pBase == nullptr) {
                    continue;
                }
                for (u32 k = 0; k < pBase->fieldCount; k++) {
                    pNames->add(pBase->fields[k].name);
                }
                gather(pBase, pNames, depth + 1);
            }
        }

        bool ownField(const TypeRow* pType, const char* pName) {
            for (u32 i = 0; i < pType->fieldCount; i++) {
                if (sameText(pType->fields[i].name, pName)) {
                    return true;
                }
            }
            return false;
        }

        // Appends pText to the reason, which always ends in a 0.
        void put(char* pWhy, u32 size, u32* pAt, const char* pText) {
            while (*pText != 0 && *pAt + 1 < size) {
                pWhy[(*pAt)++] = *pText++;
            }
            pWhy[*pAt] = 0;
        }
    }

    // A field of the type named by the first typeLength bytes of pTypeName.
    static FieldBind bindField(const char* pTypeName, u32 typeLength, const char* pField, const char* pSignature);

    void prepareTypes() {
        if (sTypes.built) {
            return;
        }
        sTypes.build(typesBegin(), typesEnd());
        sAliases.build(aliasesBegin(), aliasesEnd());
        sGlobals.build(globalsBegin(), globalsEnd());
        for (u32 group = 0; group < kLeftGroups; group++) {
            sLeft[group].build(leftBegin(), leftEnd(), group);
        }
    }

    const TypeRow* findType(const char* pName) {
        return pName != nullptr ? typeNamed(pName, textLength(pName)) : nullptr;
    }

    FieldBind findField(const char* pName, const char* pSignature) {
        FieldBind out = {};
        if (pName == nullptr) {
            return out;
        }
        const u32 dot = typeEnd(pName);
        if (pName[dot] != '.' || dot == 0 || pName[dot + 1] == 0) {
            return out;
        }
        return bindField(pName, dot, pName + dot + 1, pSignature);
    }

    FieldBind findField(const char* pType, const char* pField, const char* pSignature) {
        if (pType == nullptr || pField == nullptr || pType[0] == 0 || pField[0] == 0) {
            return FieldBind{};
        }
        return bindField(pType, textLength(pType), pField, pSignature);
    }

    static FieldBind bindField(const char* pTypeName, u32 typeLength, const char* pField, const char* pSignature) {
        FieldBind out = {};
        const TypeRow* const pType = typeNamed(pTypeName, typeLength);
        if (pType == nullptr) {
            return out;
        }
        Lookup look = {};
        lookIn(pType, pField, 0, nullptr, &look, 0);
        if (look.count == 0 || look.tooDeep) {
            return out;  // not found: the validator's left_out gives a reason if the name has one
        }
        // TY2-1: refused with left_out's own reason, "through the virtual base V" or "ambiguous: A::x, B::x" (the
        // declaring records sorted by bytes, as the generator sorts them).
        if (look.pVirtual != nullptr || look.count > 1) {
            u32 at = 0;
            out.why = look.pVirtual != nullptr ? FieldBind::kThroughVirtualBase : FieldBind::kAmbiguous;
            if (look.pVirtual != nullptr) {
                put(out.whyNames, sizeof(out.whyNames), &at, look.pVirtual);
                return out;
            }
            const u32 shown = look.count < kMostFindings ? look.count : kMostFindings;
            for (u32 i = 1; i < shown; i++) {  // insertion sort, at most kMostFindings names
                for (u32 k = i; k > 0 && joinedBefore(look.pFrom[k], look.pFrom[k - 1], pField); k--) {
                    const char* const pSwap = look.pFrom[k];
                    look.pFrom[k] = look.pFrom[k - 1];
                    look.pFrom[k - 1] = pSwap;
                }
            }
            for (u32 i = 0; i < shown; i++) {
                put(out.whyNames, sizeof(out.whyNames), &at, i == 0 ? "" : ", ");
                put(out.whyNames, sizeof(out.whyNames), &at, look.pFrom[i]);
                put(out.whyNames, sizeof(out.whyNames), &at, "::");
                put(out.whyNames, sizeof(out.whyNames), &at, pField);
            }
            return out;
        }
        const TypeField* const pRow = look.pField;
        const u32 offset = look.offset;
        out.found = true;
        // Z-4: the type text whole. A bit-field's row already reads "<type> : <width>" (TY-3), so its position in bits
        // is never taken for a byte offset.
        out.sameType = pSignature != nullptr && sameText(pSignature, pRow->type);
        out.value = pRow->bits != 0 ? static_cast<s32>(offset * 8 + pRow->bit) : static_cast<s32>(offset);
        return out;
    }

    const GlobalRow* findGlobal(const char* pName) {
        if (pName == nullptr) {
            return nullptr;
        }
        prepareTypes();
        return sGlobals.find(pName, textLength(pName));
    }

    const LeftRow* leftOut(u32 kind, const char* pName) {
        if (pName == nullptr) {
            return nullptr;
        }
        prepareTypes();
        const u32 length = textLength(pName);
        if (kind == LUMA_IMPORT_GLOBAL) {
            return sLeft[kLeftGlobal].find(pName, length);
        }
        if (kind == LUMA_IMPORT_SIZE) {
            return typeLeftOut(pName, length);
        }
        if (kind != LUMA_IMPORT_FIELD) {
            return nullptr;
        }
        const u32 dot = typeEnd(pName);
        if (pName[dot] != '.' || dot == 0 || pName[dot + 1] == 0) {
            return nullptr;
        }
        const TypeRow* const pType = typeNamed(pName, dot);
        if (pType == nullptr) {
            return typeLeftOut(pName, dot);
        }
        // The field by the record's row name, as left_out names it ("TVec3f.x" is "JGeometry::TVec3<f32>.x").
        char joined[512];
        u32 at = 0;
        put(joined, sizeof(joined), &at, pType->name);
        put(joined, sizeof(joined), &at, ".");
        put(joined, sizeof(joined), &at, pName + dot + 1);
        if (at + 1 == sizeof(joined)) {
            return nullptr;  // longer than any row's name
        }
        return sLeft[kLeftField].find(joined, at);
    }

    void describeTypes(NebulaDescribeWrite write, void* user) {
        prepareTypes();
        Out* const pOut = static_cast<Out*>(::malloc(sizeof(Out)));
        if (pOut == nullptr) {
            NEBULA_PANIC("LumaCore: no memory to describe the type rows");
        }
        pOut->write = write;
        pOut->user = user;
        pOut->used = 0;
        Out& out = *pOut;
        out.raw("{\"types\": [");
        for (u32 t = 0; t < sTypes.count; t++) {
            const TypeRow& rType = *sTypes.rows[t];
            out.raw(t == 0 ? "\n {\"name\": " : ",\n {\"name\": ");
            out.text(rType.name);
            out.raw(", \"size\": ");
            out.number(rType.size);
            out.raw(", \"align\": ");
            out.number(rType.align);
            out.raw(", \"fields\": [");
            for (u32 i = 0; i < rType.fieldCount; i++) {
                const TypeField& rField = rType.fields[i];
                out.raw(i == 0 ? "{\"name\": " : ", {\"name\": ");
                out.text(rField.name);
                out.raw(", \"type\": ");
                out.text(rField.type);
                out.raw(", \"offset\": ");
                out.number(rField.offset);
                out.raw(", \"count\": ");
                out.number(rField.count);
                out.raw(", \"bit\": ");
                out.number(rField.bit);
                out.raw(", \"bits\": ");
                out.number(rField.bits);
                out.raw("}");
            }
            out.raw("], \"bases\": [");
            for (u32 i = 0; i < rType.baseCount; i++) {
                out.raw(i == 0 ? "{\"name\": " : ", {\"name\": ");
                out.text(rType.bases[i].name);
                out.raw(", \"offset\": ");
                out.number(rType.bases[i].offset);
                out.raw(rType.bases[i].isVirtual != 0 ? ", \"virtual\": true}" : ", \"virtual\": false}");
            }
            // The kernel's own answer for each name the bases give (TY-1): what kind 1 binds or refuses.
            Names names;
            gather(&rType, &names, 0);
            u32 bound = 0;
            out.raw("], \"reachable\": [");
            for (u32 i = 0; i < names.count; i++) {
                if (ownField(&rType, names.names[i])) {
                    continue;
                }
                const FieldBind bind = bindField(rType.name, textLength(rType.name), names.names[i], nullptr);
                if (!bind.found) {
                    continue;
                }
                out.raw(bound++ == 0 ? "{\"name\": " : ", {\"name\": ");
                out.text(names.names[i]);
                out.raw(", \"value\": ");
                out.number(bind.value);
                out.raw("}");
            }
            u32 refused = 0;
            out.raw("], \"refused\": [");
            for (u32 i = 0; i < names.count; i++) {
                if (ownField(&rType, names.names[i])) {
                    continue;
                }
                const FieldBind bind = bindField(rType.name, textLength(rType.name), names.names[i], nullptr);
                if (bind.found || bind.why == FieldBind::kNone) {
                    continue;
                }
                out.raw(refused++ == 0 ? "{\"name\": " : ", {\"name\": ");
                out.text(names.names[i]);
                out.raw(bind.why == FieldBind::kAmbiguous ? ", \"why\": \"ambiguous\", \"names\": "
                                                          : ", \"why\": \"virtual\", \"names\": ");
                out.text(bind.whyNames);
                out.raw("}");
            }
            out.raw("]}");
            ::free(names.names);
        }
        out.raw("\n], \"aliases\": [");
        for (u32 a = 0; a < sAliases.count; a++) {
            out.raw(a == 0 ? "\n {\"name\": " : ",\n {\"name\": ");
            out.text(sAliases.rows[a]->name);
            out.raw(", \"record\": ");
            out.text(sAliases.rows[a]->record);
            out.raw("}");
        }
        out.raw("\n], \"globals\": [");
        for (u32 g = 0; g < sGlobals.count; g++) {
            out.raw(g == 0 ? "\n {\"name\": " : ",\n {\"name\": ");
            out.text(sGlobals.rows[g]->name);
            out.raw(", \"type\": ");
            out.text(sGlobals.rows[g]->type);
            out.raw(sGlobals.rows[g]->readOnly != 0 ? ", \"const\": true}" : ", \"const\": false}");
        }
        out.raw("\n], \"left_out\": [");
        u32 left = 0;
        for (u32 group = 0; group < kLeftGroups; group++) {
            for (u32 i = 0; i < sLeft[group].count; i++) {
                const LeftRow& rRow = *sLeft[group].rows[i];
                out.raw(left++ == 0 ? "\n {\"name\": " : ",\n {\"name\": ");
                out.text(rRow.name);
                out.raw(", \"group\": ");
                out.number(group);
                out.raw(", \"why\": ");
                out.text(rRow.why);
                if (rRow.what != nullptr) {
                    out.raw(", \"what\": ");
                    out.text(rRow.what);
                }
                out.raw("}");
            }
        }
        out.raw("\n]}\n");
        out.flush();
        ::free(pOut);
    }
}

extern "C" NEBULA_GAME_API void nebula_game_luma_describe(NebulaDescribeWrite write, void* user) {
    if (write != nullptr) {
        luma::describeTypes(write, user);
    }
}
