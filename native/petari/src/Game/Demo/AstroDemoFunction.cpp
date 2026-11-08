#include "Game/Demo/AstroDemoFunction.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"

namespace {
    const char* const cGrandStarReturnDemoTable[] = {
        "グランドスター１帰還", "グランドスター２帰還", "グランドスター３帰還",
        "グランドスター４帰還", "グランドスター５帰還", "グランドスター６帰還",
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
        for (u32 index = 0; index < ARRAY_SIZE(::cGrandStarReturnDemoTable); index++) {
            if (MR::isDemoActive(::cGrandStarReturnDemoTable[index])) {
                return index;
            }
        }

        return -1;
    }

    void tryRegisterDemo(LiveActor* pActor, const char* pDemoName, const TableIter& rIter) {
        if (MR::isDemoExist(pDemoName) && !MR::isDemoCast(pActor, pDemoName)) {
            MR::tryRegisterDemoCast(pActor, pDemoName, rIter);
        }
    }

    void tryRegisterAstroDemoAll(LiveActor* pActor, const TableIter& rIter) {
        AstroDemoFunction::tryRegisterGrandStarReturn(pActor, rIter);
        AstroDemoFunction::tryRegisterDemo(pActor, "グランドスター帰還[２回目以降]", rIter);
        AstroDemoFunction::tryRegisterDemo(pActor, "パワースター帰還", rIter);
        AstroDemoFunction::tryRegisterDemo(pActor, "ロゼッタ状況説明デモ", rIter);
        AstroDemoFunction::tryRegisterDemo(pActor, "ロゼッタ最終決戦デモ", rIter);
        AstroDemoFunction::tryRegisterDemo(pActor, "ロゼッタコメット説明デモ", rIter);
        AstroDemoFunction::tryRegisterDemo(pActor, "ロゼッタトーチの炎説明デモ", rIter);
        AstroDemoFunction::tryRegisterDemo(pActor, "ロゼッタトーチの炎進捗デモ", rIter);
        AstroDemoFunction::tryRegisterDemo(pActor, "ロゼッタ天文台機能回復デモ", rIter);
        AstroDemoFunction::tryRegisterDemo(pActor, "ロゼッタキノピオ探検隊デモ", rIter);
        AstroDemoFunction::tryRegisterDemo(pActor, "ルイージ失踪デモ", rIter);
        AstroDemoFunction::tryRegisterDemo(pActor, "ロゼッタカウントダウン開始デモ", rIter);
        AstroDemoFunction::tryRegisterDemo(pActor, "ロゼッタノーマルエンディング後デモ", rIter);
        AstroDemoFunction::tryRegisterDemo(pActor, "ロゼッタ銀河の中心説明デモ", rIter);
        AstroDemoFunction::tryRegisterDemo(pActor, "ロゼッタルイージデモ", rIter);
        AstroDemoFunction::tryRegisterDemo(pActor, "バトラーグリーンドライバ説明", rIter);
        AstroDemoFunction::tryRegisterDemo(pActor, "バトラーマップレクチャー", rIter);
    }

    void tryRegisterGrandStarReturn(LiveActor* pActor, const TableIter& rIter) {
        for (u32 i = 0; i < ARRAY_SIZE(::cGrandStarReturnDemoTable); i++) {
            AstroDemoFunction::tryRegisterDemo(pActor, ::cGrandStarReturnDemoTable[i], rIter);
        }
    }

    void tryRegisterGrandStarReturnWithFunction(LiveActor* pActor, const TableIter& rIter, const MR::FunctorBase& rFunctor) {
        const char* pDemoName;

        for (u32 i = 0; i < ARRAY_SIZE(::cGrandStarReturnDemoTable); i++) {
            pDemoName = ::cGrandStarReturnDemoTable[i];

            if (MR::isDemoExist(pDemoName) && MR::tryRegisterDemoCast(pActor, pDemoName, rIter)) {
                MR::tryRegisterDemoActionFunctorDirect(pActor, rFunctor, pDemoName, nullptr);
            }
        }
    }

    void tryRegisterGrandStarReturnAndSimpleCast(LiveActor* pActor, const TableIter& rIter) {
        AstroDemoFunction::tryRegisterGrandStarReturn(pActor, rIter);
        AstroDemoFunction::tryRegisterSimpleCastIfAstroGalaxy(pActor);
    }

    void tryRegisterGrandStarReturnWithFunctionAndSimpleCast(LiveActor* pActor, const TableIter& rIter, const MR::FunctorBase& rFunctor) {
        AstroDemoFunction::tryRegisterGrandStarReturnWithFunction(pActor, rIter, rFunctor);
        AstroDemoFunction::tryRegisterSimpleCastIfAstroGalaxy(pActor);
    }

    bool tryRegisterSimpleCastIfAstroGalaxy(LiveActor* pActor) {
        if (MR::isEqualStageName("AstroGalaxy")) {
            MR::registerDemoSimpleCastAll(pActor);

            return true;
        }

        return false;
    }

    void tryRegisterDemoForTico(LiveActor* pActor, const TableIter& rIter) {
        if (tryRegisterSimpleCastIfAstroGalaxy(pActor)) {
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
                AstroDemoFunction::tryRegisterDemo(pActor, pDemoName, rIter);
            }
        }
    }

    void tryRegisterDemoForLuigiAndKinopio(LiveActor* pActor, const TableIter& rIter) {
        AstroDemoFunction::tryRegisterGrandStarReturn(pActor, rIter);
        AstroDemoFunction::tryRegisterDemo(pActor, "ロゼッタルイージデモ", rIter);
        AstroDemoFunction::tryRegisterDemo(pActor, "バトラーグリーンドライバ説明", rIter);
        AstroDemoFunction::tryRegisterDemo(pActor, "ロゼッタ最終決戦デモ", rIter);
    }
};  // namespace AstroDemoFunction
