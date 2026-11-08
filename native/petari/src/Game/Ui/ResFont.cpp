#include "Game/Ui/ResFont.hpp"
#include "Game/Render/RenderTexture.hpp"

namespace Ui {
    ResFont::ResFont() {
    }

    ResFont::~ResFont() {
    }

    bool ResFont::SetResource(void* pBuffer) {
        if (!IsManaging(NULL) || pBuffer == NULL) {
            return false;
        }
        FontFile* pFile = static_cast< FontFile* >(pBuffer);
        if (memcmp(pFile->signature, "RFNT", 4) == 0) {
            NEBULA_PANIC("ResFont: a font the importer did not cook");
        }
        if (memcmp(pFile->signature, "NFNT", 4) != 0 || pFile->version != cFontVersion) {
            return false;
        }
        // Every glyph names a cell of a sheet: a font without them is not used.
        if (pFile->sheetNum == 0 || pFile->sheetRow == 0 || pFile->sheetLine == 0 || pFile->sheetWidth == 0 ||
            pFile->sheetHeight == 0 || pFile->glyphNum == 0) {
            return false;
        }
        SetResourceBuffer(pBuffer, pFile);
        InitReaderFunc(GetEncoding());
        return true;
    }

    void ResFont::RemoveResource() {
        RemoveResourceBuffer();
    }
}  // namespace Ui
