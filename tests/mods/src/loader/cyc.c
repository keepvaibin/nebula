/* lt-cyc-a and lt-cyc-b (loader D3-3): each manifest needs the other, so neither starts; the host names each (L800,
   Needs.Cycle). The code only says it started, which must not happen. */
#include "lt.h"

LT_START((void)0)
LUMA_MOD_NO_IMPORTS(start, 0);
