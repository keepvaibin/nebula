#include "Game/Demo/AstroDemoFunction.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/JMapUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"

namespace {
    const char* const cGrandStarReturnDemoTable[] = {
        "\203\117\203\211\203\223\203\150\203\130\203\136\201\133\202\120\213\101\212\322" /* グランドスター１帰還 */, "\203\117\203\211\203\223\203\150\203\130\203\136\201\133\202\121\213\101\212\322" /* グランドスター２帰還 */, "\203\117\203\211\203\223\203\150\203\130\203\136\201\133\202\122\213\101\212\322" /* グランドスター３帰還 */,
        "\203\117\203\211\203\223\203\150\203\130\203\136\201\133\202\123\213\101\212\322" /* グランドスター４帰還 */, "\203\117\203\211\203\223\203\150\203\130\203\136\201\133\202\124\213\101\212\322" /* グランドスター５帰還 */, "\203\117\203\211\203\223\203\150\203\130\203\136\201\133\202\125\213\101\212\322" /* グランドスター６帰還 */,
    };
};  // namespace

namespace AstroDemoFunction {
    int getOpenedAstroDomeNum() {
        return MR::calcOpenedAstroDomeNum();
    }

    const char* getGrandStarReturnDemoName(int index) {
        return ::cGrandStarReturnDemoTable[index];
    }

    int getActiveGrandStarReturnDemoIndex() {
        s32 result;
        for (u32 index = 0; index < ARRAY_SIZE(::cGrandStarReturnDemoTable); index++) {
            if (MR::isDemoActive(::cGrandStarReturnDemoTable[index])) {
                result = index;
                // only way I could get this to match
                // TODO -- fix me
                goto found;
            }
        }

        result = -1;
    found:
        return result;
    }

    void tryRegisterDemo(LiveActor* pParam1, const char* pParam2, const JMapInfoIter& rIter) {
        if (MR::isDemoExist(pParam2) && !MR::isDemoCast(pParam1, pParam2)) {
            MR::tryRegisterDemoCast(pParam1, pParam2, rIter);
        }
    }

    void tryRegisterAstroDemoAll(LiveActor* pParam1, const JMapInfoIter& rIter) {
        AstroDemoFunction::tryRegisterGrandStarReturn(pParam1, rIter);
        AstroDemoFunction::tryRegisterDemo(pParam1, "\203\117\203\211\203\223\203\150\203\130\203\136\201\133\213\101\212\322[\202\121\211\361\226\332\210\310\215\176]" /* グランドスター帰還[２回目以降] */, rIter);
        AstroDemoFunction::tryRegisterDemo(pParam1, "\203\160\203\217\201\133\203\130\203\136\201\133\213\101\212\322" /* パワースター帰還 */, rIter);
        AstroDemoFunction::tryRegisterDemo(pParam1, "\203\215\203\133\203\142\203\136\217\363\213\265\220\340\226\276\203\146\203\202" /* ロゼッタ状況説明デモ */, rIter);
        AstroDemoFunction::tryRegisterDemo(pParam1, "\203\215\203\133\203\142\203\136\215\305\217\111\214\210\220\355\203\146\203\202" /* ロゼッタ最終決戦デモ */, rIter);
        AstroDemoFunction::tryRegisterDemo(pParam1, "\203\215\203\133\203\142\203\136\203\122\203\201\203\142\203\147\220\340\226\276\203\146\203\202" /* ロゼッタコメット説明デモ */, rIter);
        AstroDemoFunction::tryRegisterDemo(pParam1, "\203\215\203\133\203\142\203\136\203\147\201\133\203\140\202\314\211\212\220\340\226\276\203\146\203\202" /* ロゼッタトーチの炎説明デモ */, rIter);
        AstroDemoFunction::tryRegisterDemo(pParam1, "\203\215\203\133\203\142\203\136\203\147\201\133\203\140\202\314\211\212\220\151\222\273\203\146\203\202" /* ロゼッタトーチの炎進捗デモ */, rIter);
        AstroDemoFunction::tryRegisterDemo(pParam1, "\203\215\203\133\203\142\203\136\223\126\225\266\221\344\213\100\224\134\211\361\225\234\203\146\203\202" /* ロゼッタ天文台機能回復デモ */, rIter);
        AstroDemoFunction::tryRegisterDemo(pParam1, "\203\215\203\133\203\142\203\136\203\114\203\155\203\163\203\111\222\124\214\237\221\340\203\146\203\202" /* ロゼッタキノピオ探検隊デモ */, rIter);
        AstroDemoFunction::tryRegisterDemo(pParam1, "\203\213\203\103\201\133\203\127\216\270\347\110\203\146\203\202" /* ルイージ失踪デモ */, rIter);
        AstroDemoFunction::tryRegisterDemo(pParam1, "\203\215\203\133\203\142\203\136\203\112\203\105\203\223\203\147\203\137\203\105\203\223\212\112\216\156\203\146\203\202" /* ロゼッタカウントダウン開始デモ */, rIter);
        AstroDemoFunction::tryRegisterDemo(pParam1, "\203\215\203\133\203\142\203\136\203\155\201\133\203\175\203\213\203\107\203\223\203\146\203\102\203\223\203\117\214\343\203\146\203\202" /* ロゼッタノーマルエンディング後デモ */, rIter);
        AstroDemoFunction::tryRegisterDemo(pParam1, "\203\215\203\133\203\142\203\136\213\342\211\315\202\314\222\206\220\123\220\340\226\276\203\146\203\202" /* ロゼッタ銀河の中心説明デモ */, rIter);
        AstroDemoFunction::tryRegisterDemo(pParam1, "\203\215\203\133\203\142\203\136\203\213\203\103\201\133\203\127\203\146\203\202" /* ロゼッタルイージデモ */, rIter);
        AstroDemoFunction::tryRegisterDemo(pParam1, "\203\157\203\147\203\211\201\133\203\117\203\212\201\133\203\223\203\150\203\211\203\103\203\157\220\340\226\276" /* バトラーグリーンドライバ説明 */, rIter);
        AstroDemoFunction::tryRegisterDemo(pParam1, "\203\157\203\147\203\211\201\133\203\175\203\142\203\166\203\214\203\116\203\140\203\203\201\133" /* バトラーマップレクチャー */, rIter);
    }

    void tryRegisterGrandStarReturn(LiveActor* pParam1, const JMapInfoIter& rIter) {
        for (u32 i = 0; i < ARRAY_SIZE(::cGrandStarReturnDemoTable); i++) {
            AstroDemoFunction::tryRegisterDemo(pParam1, ::cGrandStarReturnDemoTable[i], rIter);
        }
    }

    void tryRegisterGrandStarReturnWithFunction(LiveActor* pParam1, const JMapInfoIter& rIter, const MR::FunctorBase& rFunctor) {
        const char* pDemoName;

        for (u32 i = 0; i < ARRAY_SIZE(::cGrandStarReturnDemoTable); i++) {
            pDemoName = ::cGrandStarReturnDemoTable[i];

            if (MR::isDemoExist(pDemoName) && MR::tryRegisterDemoCast(pParam1, pDemoName, rIter)) {
                MR::tryRegisterDemoActionFunctorDirect(pParam1, rFunctor, pDemoName, nullptr);
            }
        }
    }

    void tryRegisterGrandStarReturnAndSimpleCast(LiveActor* pParam1, const JMapInfoIter& rIter) {
        AstroDemoFunction::tryRegisterGrandStarReturn(pParam1, rIter);
        AstroDemoFunction::tryRegisterSimpleCastIfAstroGalaxy(pParam1);
    }

    void tryRegisterGrandStarReturnWithFunctionAndSimpleCast(LiveActor* pParam1, const JMapInfoIter& rIter, const MR::FunctorBase& rFunctor) {
        AstroDemoFunction::tryRegisterGrandStarReturnWithFunction(pParam1, rIter, rFunctor);
        AstroDemoFunction::tryRegisterSimpleCastIfAstroGalaxy(pParam1);
    }

    bool tryRegisterSimpleCastIfAstroGalaxy(LiveActor* pParam1) {
        if (MR::isEqualStageName("AstroGalaxy")) {
            MR::registerDemoSimpleCastAll(pParam1);

            return true;
        }

        return false;
    }

    void tryRegisterDemoForTico(LiveActor* pParam1, const JMapInfoIter& rIter) {
        if (tryRegisterSimpleCastIfAstroGalaxy(pParam1)) {
            s32 demoCastID = MR::getDemoCastID(rIter);
            const char* pDemoName = nullptr;

            switch (demoCastID) {
            case 0:
                pDemoName = ::cGrandStarReturnDemoTable[0];
                break;
            case 1:
                pDemoName = ::cGrandStarReturnDemoTable[2];
                break;
            case 2:
                pDemoName = ::cGrandStarReturnDemoTable[4];
                break;
            }

            if (pDemoName != nullptr) {
                AstroDemoFunction::tryRegisterDemo(pParam1, pDemoName, rIter);
            }
        }
    }

    void tryRegisterDemoForLuigiAndKinopio(LiveActor* pParam1, const JMapInfoIter& rIter) {
        AstroDemoFunction::tryRegisterGrandStarReturn(pParam1, rIter);
        AstroDemoFunction::tryRegisterDemo(pParam1, "\203\215\203\133\203\142\203\136\203\213\203\103\201\133\203\127\203\146\203\202" /* ロゼッタルイージデモ */, rIter);
        AstroDemoFunction::tryRegisterDemo(pParam1, "\203\157\203\147\203\211\201\133\203\117\203\212\201\133\203\223\203\150\203\211\203\103\203\157\220\340\226\276" /* バトラーグリーンドライバ説明 */, rIter);
        AstroDemoFunction::tryRegisterDemo(pParam1, "\203\215\203\133\203\142\203\136\215\305\217\111\214\210\220\355\203\146\203\202" /* ロゼッタ最終決戦デモ */, rIter);
    }
};  // namespace AstroDemoFunction
