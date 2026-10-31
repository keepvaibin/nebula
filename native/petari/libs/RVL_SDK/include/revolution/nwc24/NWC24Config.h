#ifndef RVL_SDK_NWC24_CONFIG_H
#define RVL_SDK_NWC24_CONFIG_H
#include "revolution/types.h"
#include <cstdlib>

#include "revolution/nwc24/NWC24Types.h"
#ifdef __cplusplus
extern "C" {
#endif

NWC24Err NWC24GetMyUserId(NWC24UserId* pUserId);
NWC24Err NWC24GenerateNewUserId(NWC24UserId* pUserId);
const char* NWC24GetAccountDomain(void);
const char* NWC24GetMBoxDir(void);
u32 NWC24GetAppId(void);
u16 NWC24GetGroupId(void);

#ifdef __cplusplus
}
#endif
#endif
