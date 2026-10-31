#ifndef RVL_SDK_NWC24_MANAGE_H
#define RVL_SDK_NWC24_MANAGE_H
#include "revolution/types.h"
#include <cstdlib>

#include "revolution/nwc24/NWC24Types.h"

#ifdef __cplusplus
extern "C" {
#endif

NWC24Err NWC24OpenLib(void* pWork);
NWC24Err NWC24CloseLib(void);
s32 NWC24GetErrorCode(void);
BOOL NWC24IsMsgLibOpened(void);
BOOL NWC24IsMsgLibOpenedByTool(void);
BOOL NWC24IsMsgLibOpenBlocking(void);
NWC24Err NWC24BlockOpenMsgLib(BOOL block);

#ifdef __cplusplus
}
#endif
#endif
