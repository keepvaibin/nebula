#include "Game/Map/FileSelectFunc.hpp"
#include "Game/Map/FileSelectIconID.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include <MiiLib.h>

namespace {
    /// @brief The array of message identifiers corresponding to File Selection Screen icon names.
    static const char* sIconNameMessageID[] = {
        "System_FileSelect_Icon000", "System_FileSelect_Icon001", "System_FileSelect_Icon002",
        "System_FileSelect_Icon003", "System_FileSelect_Icon004",
    };
};  // namespace

namespace FileSelectFunc {
    u32 getMiiNameBufferSize() {
        return MII_NAME_LEN + 1;
    }

    void copyMiiName(u16* pName, const FileSelectIconID& rIcon) {
        if (rIcon.isFellow()) {
            const char* pMessageId = ::sIconNameMessageID[rIcon.getFellowID()];
            const char16_t* pMessage = MR::getGameMessageDirect(pMessageId);

            MR::copyMemory(pName, pMessage, getMiiNameBufferSize() * sizeof(char16_t));
        } else if (rIcon.isMii()) {
            MiiAdditionalInfo info;
            MiiErrcode err = MiiGetAdditionalInfo(&info, MiiDataSource_Official, nullptr, rIcon.getMiiIndex());

            if (err == MiiErrcode_Success) {
                MR::copyMemory(pName, info.name, getMiiNameBufferSize() * sizeof(char16_t));
            }
        }
    }
};  // namespace FileSelectFunc
