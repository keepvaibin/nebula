#include "Game/Screen/SceneWipeHolder.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/WipeFade.hpp"
#include "Game/Screen/WipeGameOver.hpp"
#include "Game/Screen/WipeKoopa.hpp"
#include "Game/Screen/WipeRing.hpp"
#include "Game/Util/ObjUtil.hpp"

SceneWipeHolder::SceneWipeHolder() : WipeHolderBase(8, "\203\126\201\133\203\223\203\217\203\103\203\166\225\333\216\235" /* シーンワイプ保持 */) {
    WipeLayoutBase* pWipeLayout;

    pWipeLayout = new WipeRing(1, "\211\176\203\217\203\103\203\166" /* 円ワイプ */);
    addWipeLayout(pWipeLayout);
    MR::connectToSceneWipeLayout(pWipeLayout);
    MR::joinToNameObjGroup(pWipeLayout, "IgnorePauseNameObj");

    pWipeLayout = new WipeFade("\203\164\203\106\201\133\203\150\203\217\203\103\203\166" /* フェードワイプ */, Color8(0, 0, 0, 255));
    addWipeLayout(pWipeLayout);
    MR::connectToSceneWipeLayout(pWipeLayout);
    MR::joinToNameObjGroup(pWipeLayout, "IgnorePauseNameObj");

    pWipeLayout = new WipeFade("\224\222\203\164\203\106\201\133\203\150\203\217\203\103\203\166" /* 白フェードワイプ */, Color8(255, 255, 255, 255));
    addWipeLayout(pWipeLayout);
    MR::connectToSceneWipeLayout(pWipeLayout);
    MR::joinToNameObjGroup(pWipeLayout, "IgnorePauseNameObj");

    pWipeLayout = new WipeGameOver();
    addWipeLayout(pWipeLayout);
    MR::connectToSceneWipeLayout(pWipeLayout);
    MR::joinToNameObjGroup(pWipeLayout, "IgnorePauseNameObj");

    pWipeLayout = new WipeKoopa();
    addWipeLayout(pWipeLayout);
    MR::connectToSceneWipeLayout(pWipeLayout);
    MR::joinToNameObjGroup(pWipeLayout, "IgnorePauseNameObj");
}

namespace SceneWipeHolderFunction {
    SceneWipeHolder* getSceneWipeHolder() {
        return MR::getSceneObj< SceneWipeHolder >(SceneObj_SceneWipeHolder);
    }

    void openWipe(const char* pWipeName, s32 frame) {
        getSceneWipeHolder()->forceClose(pWipeName);
        getSceneWipeHolder()->wipe(nullptr, frame);
    }

    void closeWipe(const char* pWipeName, s32 frame) {
        getSceneWipeHolder()->forceOpen(pWipeName);
        getSceneWipeHolder()->wipe(nullptr, frame);
    }

    void forceOpenWipe(const char* pWipeName) {
        getSceneWipeHolder()->forceOpen(pWipeName);
    }

    void forceCloseWipe(const char* pWipeName) {
        getSceneWipeHolder()->forceClose(pWipeName);
    }
};  // namespace SceneWipeHolderFunction
