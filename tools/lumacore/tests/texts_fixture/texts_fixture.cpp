// The text fixture: which `const char *` parameters are `in` (see texts_fixture.hpp); texts_fixture_test.py holds the
// expected marks.
#include "texts_fixture.hpp"

const char* sTxKept = nullptr;

// Read: a C library reader, a character, a test.
int txLen(const char* pText) { return (int)strlen(pText); }
int txCompare(const char* pA, const char* pB) { return strcmp(pA, pB) == 0; }
int txFirst(const char* pText) { return pText[0]; }
int txDeref(const char* pText) { return *pText; }
int txTest(const char* pText) { return pText != nullptr && *pText != 0; }

// A local copy, moved along: read.
int txLoop(const char* pText) {
    int n = 0;
    for (const char* p = pText; *p != 0; ++p) {
        n++;
    }
    return n;
}

// Copied out, formatted: read (pOut is no text: it is written).
void txCopy(char* pOut, const char* pText) { strcpy(pOut, pText); }
void txFormat(char* pOut, const char* pText) { snprintf(pOut, 16, "%s", pText); }

// Given to a function that reads it: read.
int txCalls(const char* pText) { return txLen(pText); }

// Kept in a global, and given to the function that keeps it.
void txKeep(const char* pText) { sTxKept = pText; }
void txCallsKeeper(const char* pText) { txKeep(pText); }

// Kept in a static local.
bool txStaticLocal(const char* pText) {
    static const char* sSeen = pText;
    return sSeen != nullptr;
}

// Returned: the caller may keep it.
const char* txReturn(const char* pText) { return pText; }

// strchr's result points into the text: read here, kept there.
int txStrchr(const char* pText) {
    const char* dot = strchr(pText, '.');
    return dot != nullptr ? (int)(dot - pText) : -1;
}
void txStrchrKept(const char* pText) { sTxKept = strchr(pText, '.'); }

// Its address taken; captured by a lambda; cast to a number; given to a call through a function pointer.
int txAddress(const char* pText) {
    const char** pp = &pText;
    return **pp;
}
int txLambda(const char* pText) {
    auto first = [pText] { return (int)*pText; };
    return first();
}
bool txCast(const char* pText) { return (unsigned long long)pText != 0; }
int txPointer(const char* pText, int (*pFn)(const char*)) { return pFn(pText); }

// A cycle of calls that only reads: read.
int txRecursive(const char* pText, int n) { return n > 0 ? txRecursive(pText, n - 1) : *pText; }
int txMutualA(const char* pText, int n) { return n > 0 ? txMutualB(pText, n - 1) : *pText; }
int txMutualB(const char* pText, int n) { return n > 0 ? txMutualA(pText, n - 1) : *pText; }
// A cycle where one keeps: both keep.
int txKeepsA(const char* pText, int n) { return n > 0 ? txKeepsB(pText, n - 1) : *pText; }
int txKeepsB(const char* pText, int n) {
    sTxKept = pText;
    return n > 0 ? txKeepsA(pText, n - 1) : 0;
}

// A member initializer keeps it; a constructor that keeps it.
TxName::TxName(const char* pName) : name(pName) {}
bool txMakesName(const char* pText) {
    TxName made(pText);
    return made.name != nullptr;
}

TxBase::~TxBase() {}
int TxBase::look(const char* pText) { return *pText; }
int TxBase::keep(const char* pText) { return *pText; }
int TxKeeper::look(const char* pText) { return pText[1]; }
int TxKeeper::keep(const char* pText) {
    kept = pText;
    return 0;
}
int TxKeeper::pure(const char* pText) {
    kept = pText;
    return 1;
}

// Through a virtual function: every override counts, at every level.
int txLook(TxBase* pBase, const char* pText) { return pBase->look(pText); }
int txKeepThrough(TxBase* pBase, const char* pText) { return pBase->keep(pText); }
int txPure(TxBase* pBase, const char* pText) { return pBase->pure(pText); }
int txOwnLook(TxKeeper* pKeeper, const char* pText) { return pKeeper->look(pText); }

// Past a variadic function's named parameters: let out. A variadic function's own: not marked.
int txSum(int n, ...) {
    va_list args;
    va_start(args, n);
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += va_arg(args, int);
    }
    va_end(args);
    return sum;
}
int txGivesToDots(const char* pText) { return txSum(1, pText); }
int txLog(const char* pFormat, ...) {
    char line[32];
    va_list args;
    va_start(args, pFormat);
    int n = vsnprintf(line, sizeof(line), pFormat, args);
    va_end(args);
    return n;
}

// A copy made after its use in a loop: the copy's first use keeps it.
void txLateCopy(const char* pText) {
    const char* last = "none";
    for (int i = 0; i < 2; i++) {
        sTxKept = last;
        last = pText;
    }
}

// Kept through ?:; kept in an initializer list.
void txBranch(const char* pText, bool b) { sTxKept = b ? pText : "other"; }
int txList(const char* pText) {
    const char* list[] = {pText};
    return list[0][0];
}

// An address inside the text: kept, or read.
void txInsideKept(const char* pText) { sTxKept = &pText[1]; }
int txInsideRead(const char* pText) { return (int)strlen(&pText[1]); }

// Two texts: one read, one kept.
int txTwo(const char* pRead, const char* pKept) {
    sTxKept = pKept;
    return *pRead;
}

int txUsesHeaderRead(const char* pText) { return txHeaderCompare(pText, "a"); }
void txUsesHeaderKeep(const char* pText) { txHeaderKeep(pText); }
int txSubstring(const char* pText, const char* pPart) { return strstr(pText, pPart) != nullptr; }

TxShown::~TxShown() {}
int TxShown::show(const char* pText) { return *pText; }
int txShow(TxShown* pShown, const char* pText) { return pShown->show(pText); }
