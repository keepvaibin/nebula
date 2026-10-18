#include "Game/System/HomeButtonMenuWrapper.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include <JSystem/JKernel/JKRExpHeap.hpp>
#include <revolution/rso.h>

void (*HBMCreateRSO)(const HBMDataInfo*);
void (*HBMInitRSO)(void);
void (*HBMCalcRSO)(const HBMControllerData*);
void (*HBMDrawRSO)(void);
HBMSelectBtnNum (*HBMGetSelectBtnNumRSO)(void);
void (*HBMSetAdjustFlagRSO)(int);
void (*HBMStartBlackOutRSO)(void);

#ifdef TARGET_PC
#define NEBULA_RSO_PTR(f) ((void**)&f)
#else
#define NEBULA_RSO_PTR(f) ((u32*)&f)
#endif
static RSOExportFuncTable exp_tbl[] = {{"HBMCreateRSO", NEBULA_RSO_PTR(HBMCreateRSO)},
                                       {"HBMInitRSO", NEBULA_RSO_PTR(HBMInitRSO)},
                                       {"HBMCalcRSO", NEBULA_RSO_PTR(HBMCalcRSO)},
                                       {"HBMDrawRSO", NEBULA_RSO_PTR(HBMDrawRSO)},
                                       {"HBMGetSelectBtnNumRSO", NEBULA_RSO_PTR(HBMGetSelectBtnNumRSO)},
                                       {"HBMSetAdjustFlagRSO", NEBULA_RSO_PTR(HBMSetAdjustFlagRSO)},
                                       {"HBMStartBlackOutRSO", NEBULA_RSO_PTR(HBMStartBlackOutRSO)}};

typedef void (*ProloguePtr)(BOOL);

#ifdef TARGET_PC
// The HOME Menu library is a PowerPC module on the disc; natively its entry points are Nebula's (native/sdk/hbm),
// which the host's own HOME Menu serves.
extern "C" {
void NebulaHBMCreate(const HBMDataInfo*);
void NebulaHBMInit(void);
void NebulaHBMCalc(const HBMControllerData*);
void NebulaHBMDraw(void);
HBMSelectBtnNum NebulaHBMGetSelectBtnNum(void);
void NebulaHBMSetAdjustFlag(int);
void NebulaHBMStartBlackOut(void);
}

void RSO::setupRsoHomeButtonMenu() {
    HBMCreateRSO = NebulaHBMCreate;
    HBMInitRSO = NebulaHBMInit;
    HBMCalcRSO = NebulaHBMCalc;
    HBMDrawRSO = NebulaHBMDraw;
    HBMGetSelectBtnNumRSO = NebulaHBMGetSelectBtnNum;
    HBMSetAdjustFlagRSO = NebulaHBMSetAdjustFlag;
    HBMStartBlackOutRSO = NebulaHBMStartBlackOut;
}
#else
void RSO::setupRsoHomeButtonMenu() {
    u32 i;
    RSOObjectHeader* rsoPtr;
    RSOExportFuncTable* pTbl;
    const RSOObjectHeader* symbolTable = reinterpret_cast< const RSOObjectHeader* >(MR::receiveFile("/ModuleData/product.sel"));
    int jumpCodeSize;
    void* bss;
    void* jumps;

    RSOListInit((void*)symbolTable);
    if (symbolTable != nullptr) {
        jumpCodeSize = RSOGetJumpCodeSize(symbolTable);

        if (jumpCodeSize == 0) {
            jumps = nullptr;
        } else {
            jumps = new (MR::getStationedHeapGDDR3(), 0) char[jumpCodeSize];
            RSOMakeJumpCode(symbolTable, jumps);
        }
        rsoPtr = reinterpret_cast< RSOObjectHeader* >(MR::receiveFile("/ModuleData/HomeButtonMenuWrapperRSO.rso"));
        if (rsoPtr->mBssSize) {
            bss = new (MR::getStationedHeapGDDR3(), 0) char[rsoPtr->mBssSize];
        }

        RSOLinkList((void*)rsoPtr, bss);

        if (rsoPtr != nullptr) {
            RSOLinkJump(rsoPtr, symbolTable, jumps);
            reinterpret_cast< ProloguePtr >(rsoPtr->mProlog)(RSOIsImportSymbolResolvedAll(rsoPtr));
            for (i = 0; i < ARRAY_SIZE(exp_tbl); i++) {
                pTbl = &exp_tbl[i];
                RSOFindExportSymbolAddr(rsoPtr, pTbl->symbol_name);
#ifdef TARGET_PC
                *(pTbl->symbol_ptr) = RSOFindExportSymbolAddr(rsoPtr, pTbl->symbol_name);
#else
                *(pTbl->symbol_ptr) = (u32)RSOFindExportSymbolAddr(rsoPtr, pTbl->symbol_name);
#endif
            }
        }
    }
}
#endif

void RSO::HBMCreate(const HBMDataInfo* pHBInfo) {
    (*HBMCreateRSO)(pHBInfo);
}

void RSO::HBMInit() {
    (*HBMInitRSO)();
}

void RSO::HBMCalc(const HBMControllerData* pController) {
    (*HBMCalcRSO)(pController);
}

void RSO::HBMDraw() {
    (*HBMDrawRSO)();
}

HBMSelectBtnNum RSO::HBMGetSelectBtnNum() {
    return (*HBMGetSelectBtnNumRSO)();
}

void RSO::HBMSetAdjustFlag(int flag) {
    (*HBMSetAdjustFlagRSO)(flag);
}

void RSO::HBMStartBlackOut() {
    (*HBMStartBlackOutRSO)();
}

void _unresolved(void) {
}
