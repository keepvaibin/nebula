#include "Game/Map/FileSelectFunc.hpp"
#include "Game/Map/FileSelectIconID.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Mii/Mii.hpp"

namespace {
    /// @brief The array of message identifiers corresponding to File Selection Screen icon names.
    const char* const sIconNameMessageID[] = {
        "System_FileSelect_Icon000", "System_FileSelect_Icon001", "System_FileSelect_Icon002",
        "System_FileSelect_Icon003", "System_FileSelect_Icon004",
    };
};  // namespace

namespace FileSelectFunc {
    u32 getMiiNameBufferSize() {
        return Mii::NameLength + 1;
    }

    void copyMiiName(u16* pName, const FileSelectIconID& rIcon) {
        if (rIcon.isFellow()) {
            const char* pMessageId = ::sIconNameMessageID[rIcon.getFellowID()];
            const char16_t* pMessage = MR::getGameMessageDirect(pMessageId);

            MR::copyMemory(pName, pMessage, getMiiNameBufferSize() * sizeof(char16_t));
        } else if (rIcon.isMii()) {
            Mii::Character mii;

            if (Mii::character(Mii::Ref{Mii::SourceLibrary, rIcon.getMiiIndex()}, &mii)) {
                Mii::copyName(mii, reinterpret_cast< char16_t* >(pName));
            }
        }
    }
};  // namespace FileSelectFunc
