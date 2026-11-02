#include "Game/Screen/NebulaMaterialSweep.hpp"
#include "Game/System/FileRipper.hpp"
#include "Game/System/LayoutHolder.hpp"
#include "Game/System/NebulaDisc.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/System/ResourceHolder.hpp"
#include <JSystem/JKernel/JKRExpHeap.hpp>
#include <JSystem/JKernel/JKRMemArchive.hpp>
#include <JSystem/JUtility/JUTNativeLog.hpp>
#include <cstdio>
#include <cstring>
#include <nebula_game_api.h>
#include <nw4r/lyt/layout.h>
#include <nw4r/lyt/material.h>
#include <nw4r/lyt/pane.h>
#include <nw4r/lyt/window.h>

// Tests only (GALAXY_DUMP_MATERIAL_PROGRAMS): the generated material set needs every material on the disc, not the ones a
// tour happens to draw. A material's program depends on its data, whether the pane's vertices tint it and whether its colour is
// white: each layout's materials are set up in all of those, through the game's own Material::SetupGX, which registers what the
// set lacks. Models register theirs when they load (J3DNativeMaterial::sweep).
namespace {
    u32 sLayouts;
    u32 sMaterials;
    u32 sModels;
    u32 sSkipped;
    // The largest archive on the disc with room for what it builds; untouched outside the sweep. The game's own heaps are full.
    alignas(32) u8 sHeapMemory[0x4000000];

    void visitMaterial(nw4r::lyt::Material* pMaterial) {
        if (pMaterial == nullptr) {
            return;
        }
        sMaterials++;
        pMaterial->SetupGX(false, 255);
        pMaterial->SetupGX(true, 255);
        pMaterial->SetupGX(false, 128);
        pMaterial->SetupGX(true, 128);
    }

    void visitPane(nw4r::lyt::Pane* pPane) {
        visitMaterial(pPane->GetMaterial());
        if (nw4r::lyt::Window* pWindow = nw4r::ut::DynamicCast< nw4r::lyt::Window* >(pPane)) {
            for (u32 i = 0; pWindow->GetFrameMaterial(i) != nullptr; i++) {
                visitMaterial(pWindow->GetFrameMaterial(i));
            }
        }
        for (nw4r::lyt::PaneList::Iterator it = pPane->GetChildList().GetBeginIter(); it != pPane->GetChildList().GetEndIter(); ++it) {
            visitPane(&*it);
        }
    }

    bool endsWith(const char* pName, const char* pEnd) {
        const size_t name = strlen(pName);
        const size_t end = strlen(pEnd);
        if (name < end) {
            return false;
        }
        for (size_t i = 0; i < end; i++) {
            const char c = pName[name - end + i];
            if ((c >= 'A' && c <= 'Z' ? c + 32 : c) != pEnd[i]) {
                return false;
            }
        }
        return true;
    }

    // One archive on a heap of its own, gone afterwards.
    void sweepArchive(const char* pPath, bool layouts) {
        JUTNativeLog("material sweep: %s\n", pPath);
        JKRHeap* const pPrevious = JKRHeap::getCurrentHeap();
        JKRExpHeap* const pHeap = JKRExpHeap::create(sHeapMemory, sizeof(sHeapMemory), JKRHeap::sRootHeap, false);
        if (pHeap == nullptr) {
            sSkipped++;
            return;
        }
        pHeap->becomeCurrentHeap();
        const s32 fileSize = NebulaDisc::find(pPath);
        NebulaDisc::File file;
        void* const pData = fileSize >= 0 && NebulaDisc::open(pPath, &file) && file.mLength + 0x100000u < static_cast< u32 >(pHeap->getFreeSize())
                                ? FileRipper::loadToMainRAM(pPath, nullptr, pHeap, FileRipper::UNK_1)
                                : nullptr;
        // Only cooked archives ('RARC' as a host number): the console HOME menu's packages are another format, and unused.
        if (pData == nullptr || *static_cast< const u32* >(pData) != 'RARC') {
            sSkipped++;
        } else {
            JKRMemArchive* const pArchive = new (pHeap, 4) JKRMemArchive();
            pArchive->mountFixed(pData, JKR_MEM_BREAK_FLAG_0);
            if (layouts) {
                LayoutHolder* const pHolder = new (pHeap, 4) LayoutHolder(*pArchive);
                for (u32 i = 0; i < pHolder->mLayoutRes.mCount; i++) {
                    nw4r::lyt::Layout* const pLayout = new (pHeap, 4) nw4r::lyt::Layout();
                    if (pLayout->Build(pHolder->mLayoutRes.getRes(i), pHolder) && pLayout->mpRootPane != nullptr) {
                        sLayouts++;
                        visitPane(pLayout->mpRootPane);
                    }
                }
            } else {
                // Its models load as the game loads them: each registers its materials (ResourceHolder, J3DNativeMaterial::sweep).
                new (pHeap, 4) ResourceHolder(*pArchive);
                sModels++;
            }
        }
        pPrevious->becomeCurrentHeap();
        JKRHeap::destroy(pHeap);
    }

    void sweepFolder(const char* pFolder, bool layouts) {
        char path[256];
        NebulaDisc::FolderEntry entry;
        for (u32 i = 0; NebulaDisc::folderEntry(pFolder, i, &entry); i++) {
            if (!entry.mIsFolder && endsWith(entry.mName, ".arc")) {
                snprintf(path, sizeof(path), "%s/%s", pFolder, entry.mName);
                sweepArchive(path, layouts);
            }
        }
    }
}  // namespace

namespace NebulaMaterialSweep {
    void run() {
        static bool sDone;
        if (sDone || NebulaHost::option(NEBULA_OPTION_TEST_MATERIAL_SWEEP) == 0) {
            return;
        }
        sDone = true;
        sweepFolder("/LayoutData", true);
        char path[64];
        NebulaDisc::FolderEntry entry;
        for (u32 i = 0; NebulaDisc::folderEntry("/", i, &entry); i++) {
            if (entry.mIsFolder) {
                snprintf(path, sizeof(path), "/%s/LayoutData", entry.mName);
                if (NebulaDisc::find(path) >= 0) {
                    sweepFolder(path, true);
                }
            }
        }
        sweepFolder("/ObjectData", false);
        JUTNativeLog("material sweep: %u layouts, %u materials, %u model archives, %u archives skipped\n", sLayouts, sMaterials, sModels, sSkipped);
    }
}  // namespace NebulaMaterialSweep
