// The text fixture (tools/lumacore/texts_fixture_test.py, N-4002): one function per rule of the proof that a
// `const char *` parameter is read during the call only (instrument.py TextUses, gen_types.py prove_text_in).
#pragma once
#include <cstdarg>
#include <cstdio>
#include <cstring>

extern const char* sTxKept;

// Keeps its name (as NameObj does): a member initializer.
struct TxName {
    const char* name;
    TxName(const char* pName);
};

class TxBase {
public:
    virtual ~TxBase();
    virtual int look(const char* pText);  // read here and in every override: `in`
    virtual int keep(const char* pText);  // read here, kept by TxKeeper's override
    virtual int pure(const char* pText) = 0;
};

// Declares pure() again, still pure: an override no row has, between TxBase and TxDeep.
class TxMid : public TxBase {
public:
    int pure(const char* pText) override = 0;
};

class TxKeeper : public TxMid {
public:
    const char* kept;
    int look(const char* pText) override;
    int keep(const char* pText) override;
    int pure(const char* pText) override;  // keeps it
};

int txLen(const char* pText);
int txCompare(const char* pA, const char* pB);
int txFirst(const char* pText);
int txDeref(const char* pText);
int txTest(const char* pText);
int txLoop(const char* pText);
void txCopy(char* pOut, const char* pText);
void txFormat(char* pOut, const char* pText);
int txCalls(const char* pText);
void txKeep(const char* pText);
void txCallsKeeper(const char* pText);
bool txStaticLocal(const char* pText);
const char* txReturn(const char* pText);
int txStrchr(const char* pText);
void txStrchrKept(const char* pText);
int txAddress(const char* pText);
int txLambda(const char* pText);
bool txCast(const char* pText);
int txPointer(const char* pText, int (*pFn)(const char*));
int txRecursive(const char* pText, int n);
int txMutualA(const char* pText, int n);
int txMutualB(const char* pText, int n);
int txKeepsA(const char* pText, int n);
int txKeepsB(const char* pText, int n);
bool txMakesName(const char* pText);
int txLook(TxBase* pBase, const char* pText);
int txKeepThrough(TxBase* pBase, const char* pText);
int txPure(TxBase* pBase, const char* pText);
int txSum(int n, ...);
int txGivesToDots(const char* pText);
int txLog(const char* pFormat, ...);
void txLateCopy(const char* pText);
void txBranch(const char* pText, bool b);
int txList(const char* pText);
void txInsideKept(const char* pText);
int txInsideRead(const char* pText);
int txOwnLook(TxKeeper* pKeeper, const char* pText);
int txTwo(const char* pRead, const char* pKept);
