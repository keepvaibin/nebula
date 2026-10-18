#pragma once

#include "Game/Animation/XanimeResource.hpp"

XanimeBckTable4 quadAnimeTable[] = {
    {
        {"\212\356\226\173" /* 基本 */},        // mParent
        "WalkSoft",      // fileName1
        0.00000000000f,  // 0x8
        "Walk",          // fileName2
        0.00000000000f,  // 0x10
        "Run",           // fileName3
        0.00000000000f,  // 0x18
        "Wait",          // fileName4
        1.00000000000f,  // 0x20
    },
    {
        {"\220\205\211\152\212\356\226\173" /* 水泳基本 */},          // mParent
        "SwimWait",            // fileName1
        1.00000000000f,        // 0x8
        "SwimFlutterSurface",  // fileName2
        0.00000000000f,        // 0x10
        "SwimFlutter",         // fileName3
        0.00000000000f,        // 0x18
        "SwimDrift",           // fileName4
        0.00000000000f,        // 0x20
    },
    {
        {"\203\145\203\152\203\130\212\356\226\173" /* テニス基本 */},  // mParent
        "WalkSoft",      // fileName1
        0.00000000000f,  // 0x8
        "Walk",          // fileName2
        0.00000000000f,  // 0x10
        "TennisRun",     // fileName3
        0.00000000000f,  // 0x18
        "TennisWait",    // fileName4
        1.00000000000f,  // 0x20
    },
    {
        {"\203\130\203\211\203\103\203\137\201\133\220\113" /* スライダー尻 */},    // mParent
        "SlideHipForWard",   // fileName1
        0.300000011921f,     // 0x8
        "SlideHipBackward",  // fileName2
        0.200000002980f,     // 0x10
        "SlideHipLeft",      // fileName3
        0.250000000000f,     // 0x18
        "SlideHipRight",     // fileName4
        0.250000000000f,     // 0x20
    },
    {
        {"\215\342\202\267\202\327\202\350\211\272\214\374\202\253\202\240\202\250\202\336\202\257" /* 坂すべり下向きあおむけ */},  // mParent
        "SlideHipForWard",           // fileName1
        0.300000011921f,             // 0x8
        "SlideHipBackward",          // fileName2
        0.200000002980f,             // 0x10
        "SlideHipLeft",              // fileName3
        0.250000000000f,             // 0x18
        "SlideHipRight",             // fileName4
        0.250000000000f,             // 0x20
    },
    {
        {"\203\136\203\175\203\122\203\215\210\332\223\256" /* タマコロ移動 */},  // mParent
        "BallIdle",        // fileName1
        1.00000000000f,    // 0x8
        "BallWalkSoft",    // fileName2
        0.00000000000f,    // 0x10
        "BallWalk",        // fileName3
        0.00000000000f,    // 0x18
        "BallRun",         // fileName4
        0.00000000000f,    // 0x20
    },
    {
        {""},            // mParent
        "",              // fileName1
        0.00000000000f,  // 0x8
        nullptr,         // fileName2
        0.00000000000f,  // 0x10
        nullptr,         // fileName3
        0.00000000000f,  // 0x18
        nullptr,         // fileName4
        0.00000000000f,  // 0x20
    },
};

XanimeBckTable3 tripleAnimeTable[] = {
    {
        {"\215\342\215\266\211\105\203\105\203\107\203\103\203\147" /* 坂左右ウエイト */},  // mParent
        "Wait",              // fileName1
        1.00000000000f,      // 0x8
        "WaitSlopeL",        // fileName2
        0.00000000000f,      // 0x10
        "WaitSlopeR",        // fileName3
        0.00000000000f,      // 0x18
    },
    {
        {"\215\342\221\117\214\343\203\105\203\107\203\103\203\147" /* 坂前後ウエイト */},  // mParent
        "Wait",              // fileName1
        1.00000000000f,      // 0x8
        "WaitSlopeD",        // fileName2
        0.00000000000f,      // 0x10
        "WaitSlopeU",        // fileName3
        0.00000000000f,      // 0x18
    },
    {
        {"\203\124\201\133\203\164\203\102\203\223" /* サーフィン */},   // mParent
        "SurfRideLoop",   // fileName1
        1.00000000000f,   // 0x8
        "SurfRideLoopL",  // fileName2
        0.00000000000f,   // 0x10
        "SurfRideLoopR",  // fileName3
        0.00000000000f,   // 0x18
    },
    {
        {"\203\124\201\133\203\164\203\102\203\223\201\151\211\301\221\254\201\152" /* サーフィン（加速） */},  // mParent
        "SurfRideDashLoop",      // fileName1
        1.00000000000f,          // 0x8
        "SurfRideDashLoopL",     // fileName2
        0.00000000000f,          // 0x10
        "SurfRideDashLoopR",     // fileName3
        0.00000000000f,          // 0x18
    },
    {
        {"\203\124\201\133\203\164\203\102\203\223\214\130\202\253\212\112\216\156" /* サーフィン傾き開始 */},  // mParent
        "SurfRide",              // fileName1
        1.00000000000f,          // 0x8
        "SurfRideL",             // fileName2
        0.00000000000f,          // 0x10
        "SurfRideR",             // fileName3
        0.00000000000f,          // 0x18
    },
    {
        {"\203\124\201\133\203\164\203\102\203\223\214\130\202\253\212\112\216\156\201\151\211\301\221\254\201\152" /* サーフィン傾き開始（加速） */},  // mParent
        "SurfRideDash",                  // fileName1
        1.00000000000f,                  // 0x8
        "SurfRideDashL",                 // fileName2
        0.00000000000f,                  // 0x10
        "SurfRideDashR",                 // fileName3
        0.00000000000f,                  // 0x18
    },
    {
        {"\203\124\201\133\203\164\203\102\203\223\227\216\211\272" /* サーフィン落下 */},  // mParent
        "SurfFall",          // fileName1
        1.00000000000f,      // 0x8
        "SurfFallL",         // fileName2
        0.00000000000f,      // 0x10
        "SurfFallR",         // fileName3
        0.00000000000f,      // 0x18
    },
    {
        {""},            // mParent
        "",              // fileName1
        0.00000000000f,  // 0x8
        nullptr,         // fileName2
        0.00000000000f,  // 0x10
        nullptr,         // fileName3
        0.00000000000f,  // 0x18
    },
};

XanimeBckTable2 doubleAnimeTable[] = {
    {
        {"\202\265\202\341\202\252\202\335\212\356\226\173" /* しゃがみ基本 */},  // mParent
        "SquatWait",       // fileName1
        1.00000000000f,    // 0x8
        "SquatWalk",       // fileName2
        0.00000000000f,    // 0x10
    },
    {
        {"\202\273\202\314\217\352\221\253\223\245\202\335" /* その場足踏み */},  // mParent
        "Run",             // fileName1
        0.750000000000f,   // 0x8
        "Wait",            // fileName2
        0.250000000000f,   // 0x10
    },
    {
        {"\203\136\203\175\203\122\203\215\202\265\202\341\202\252\202\335" /* タマコロしゃがみ */},  // mParent
        "BallSquat",           // fileName1
        1.00000000000f,        // 0x8
        "BallWalk",            // fileName2
        0.00000000000f,        // 0x10
    },
    {
        {""},            // mParent
        "",              // fileName1
        0.00000000000f,  // 0x8
        nullptr,         // fileName2
        0.00000000000f,  // 0x10
    },
};

XanimeBckTable1 singleAnimeTable[] = {
    {
        {"\203\127\203\203\203\223\203\166" /* ジャンプ */},  // mParent
        "Jump",        // fileName
        0,             // animationHash
        0,             // fileHash
    },
    {
        {"\203\127\203\203\203\223\203\166B" /* ジャンプB */},  // mParent
        "Jump2",        // fileName
        0,              // animationHash
        0,              // fileHash
    },
    {
        {"\203\127\203\203\203\223\203\166C" /* ジャンプC */},  // mParent
        "JumpRoll",     // fileName
        0,              // animationHash
        0,              // fileHash
    },
    {
        {"\203\136\201\133\203\223\203\127\203\203\203\223\203\166" /* ターンジャンプ */},  // mParent
        "JumpTurn",          // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\213\363\222\206\210\352\211\361\223\135" /* 空中一回転 */},  // mParent
        "AirControl",    // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\203\164\203\212\201\133\203\127\203\203\203\223\203\166" /* フリージャンプ */},  // mParent
        "Rise",              // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\203\167\203\212\203\122\203\166\203\136\201\133\203\127\203\203\203\223\203\166" /* ヘリコプタージャンプ */},  // mParent
        "FlickAir",                // fileName
        0,                         // animationHash
        0,                         // fileHash
    },
    {
        {"\203\137\203\142\203\126\203\205\203\127\203\203\203\223\203\166" /* ダッシュジャンプ */},  // mParent
        "Rolling",             // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\224\362\202\321\202\267\202\263\202\350\203\127\203\203\203\223\203\166" /* 飛びすさりジャンプ */},  // mParent
        "Bounce",                // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\203\126\203\207\201\133\203\147\203\127\203\203\203\223\203\166" /* ショートジャンプ */},  // mParent
        "GravityChange",       // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\203\130\203\112\203\103\203\211\203\165\203\127\203\203\203\223\203\166" /* スカイラブジャンプ */},  // mParent
        "JumpTwin",              // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\203\130\203\120\203\114\203\210" /* スケキヨ */},  // mParent
        "Bury",        // fileName
        0,             // animationHash
        0,             // fileHash
    },
    {
        {"\203\130\203\120\203\114\203\210\222\105\217\157" /* スケキヨ脱出 */},  // mParent
        "Bury",            // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\226\204\202\334\202\350" /* 埋まり */},       // mParent
        "BuryStandWait",  // fileName
        0,                // animationHash
        0,                // fileHash
    },
    {
        {"\226\204\202\334\202\350\222\105\217\157" /* 埋まり脱出 */},  // mParent
        "BuryStandEnd",  // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\213\164\222\205\222\156" /* 逆着地 */},       // mParent
        "CannonFlyLand",  // fileName
        0,                // animationHash
        0,                // fileHash
    },
    {
        {"\203\172\203\142\203\160\201\133\203\127\203\203\203\223\203\166A" /* ホッパージャンプA */},  // mParent
        "HopperWaitA",          // fileName
        0,                      // animationHash
        0,                      // fileHash
    },
    {
        {"\203\172\203\142\203\160\201\133\203\127\203\203\203\223\203\166B" /* ホッパージャンプB */},  // mParent
        "HopperWaitB",          // fileName
        0,                      // animationHash
        0,                      // fileHash
    },
    {
        {"\203\172\203\142\203\160\201\133\202\323\202\335\203\127\203\203\203\223\203\166A" /* ホッパーふみジャンプA */},  // mParent
        "HopperWaitA",              // fileName
        0,                          // animationHash
        0,                          // fileHash
    },
    {
        {"\203\172\203\142\203\160\201\133\202\323\202\335\203\127\203\203\203\223\203\166B" /* ホッパーふみジャンプB */},  // mParent
        "HopperWaitB",              // fileName
        0,                          // animationHash
        0,                          // fileHash
    },
    {
        {"\203\172\203\142\203\160\201\133\210\332\223\256A" /* ホッパー移動A */},  // mParent
        "HopperRunA",       // fileName
        0,                  // animationHash
        0,                  // fileHash
    },
    {
        {"\203\172\203\142\203\160\201\133\210\332\223\256B" /* ホッパー移動B */},  // mParent
        "HopperRunB",       // fileName
        0,                  // animationHash
        0,                  // fileHash
    },
    {
        {"\203\172\203\142\203\160\201\133\203\156\203\103\203\127\203\203\203\223\203\166A" /* ホッパーハイジャンプA */},  // mParent
        "HopperJumpA",              // fileName
        0,                          // animationHash
        0,                          // fileHash
    },
    {
        {"\203\172\203\142\203\160\201\133\203\156\203\103\203\127\203\203\203\223\203\166B" /* ホッパーハイジャンプB */},  // mParent
        "HopperJumpB",              // fileName
        0,                          // animationHash
        0,                          // fileHash
    },
    {
        {"\203\172\203\142\203\160\201\133\225\307\203\127\203\203\203\223\203\166" /* ホッパー壁ジャンプ */},  // mParent
        "HopperWallJump",        // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\203\172\203\142\203\160\201\133\203\161\203\142\203\166\203\150\203\215\203\142\203\166\212\112\216\156" /* ホッパーヒップドロップ開始 */},  // mParent
        "HopperMarioHipDropStart",       // fileName
        0,                               // animationHash
        0,                               // fileHash
    },
    {
        {"\203\172\203\142\203\160\201\133\203\161\203\142\203\166\203\150\203\215\203\142\203\166" /* ホッパーヒップドロップ */},  // mParent
        "HopperMarioHipDrop",        // fileName
        0,                           // animationHash
        0,                           // fileHash
    },
    {
        {"\203\127\203\203\203\223\203\166\202\323\202\3351" /* ジャンプふみ1 */},  // mParent
        "Jump",             // fileName
        0,                  // animationHash
        0,                  // fileHash
    },
    {
        {"\203\127\203\203\203\223\203\166\202\323\202\3352" /* ジャンプふみ2 */},  // mParent
        "JumpPress2nd",     // fileName
        0,                  // animationHash
        0,                  // fileHash
    },
    {
        {"\203\127\203\203\203\223\203\166\202\323\202\3353" /* ジャンプふみ3 */},  // mParent
        "JumpPress3rd",     // fileName
        0,                  // animationHash
        0,                  // fileHash
    },
    {
        {"\210\370\202\253\226\337\202\265" /* 引き戻し */},  // mParent
        "PullBack",    // fileName
        0,             // animationHash
        0,             // fileHash
    },
    {
        {"\210\370\202\253\226\337\202\265\222\205\222\156" /* 引き戻し着地 */},  // mParent
        "PullBackLand",    // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\203\174\203\142\203\150\203\217\201\133\203\166\212\112\216\156" /* ポッドワープ開始 */},  // mParent
        "WarpPodStart",        // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\203\174\203\142\203\150\203\217\201\133\203\166\217\111\227\271" /* ポッドワープ終了 */},  // mParent
        "WarpPodEnd",          // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\225\235\202\306\202\321" /* 幅とび */},   // mParent
        "JumpBroad",  // fileName
        0,            // animationHash
        0,            // fileHash
    },
    {
        {"\222\205\222\156" /* 着地 */},  // mParent
        "Land",    // fileName
        0,         // animationHash
        0,         // fileHash
    },
    {
        {"\222\205\222\156B" /* 着地B */},    // mParent
        "Jump2Land",  // fileName
        0,            // animationHash
        0,            // fileHash
    },
    {
        {"\222\205\222\156C" /* 着地C */},       // mParent
        "JumpRollLand",  // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\222\205\222\156\203\136\201\133\203\223" /* 着地ターン */},  // mParent
        "JumpTurnLand",  // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\222\205\222\156\225\235\202\306\202\321" /* 着地幅とび */},   // mParent
        "JumpBroadLand",  // fileName
        0,                // animationHash
        0,                // fileHash
    },
    {
        {"\203\156\201\133\203\150\222\205\222\156" /* ハード着地 */},  // mParent
        "LandStiffen",   // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\202\267\202\327\202\350\222\205\222\156" /* すべり着地 */},  // mParent
        "LandSlope",     // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\203\126\203\207\201\133\203\147\222\205\222\156" /* ショート着地 */},  // mParent
        "Land",            // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\203\161\203\142\203\166\203\150\203\215\203\142\203\166\212\112\216\156" /* ヒップドロップ開始 */},  // mParent
        "HipDropStart",          // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\203\161\203\142\203\166\203\150\203\215\203\142\203\166" /* ヒップドロップ */},  // mParent
        "HipDrop",           // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\203\161\203\142\203\166\203\150\203\215\203\142\203\166\222\205\222\156" /* ヒップドロップ着地 */},  // mParent
        "HipDropLand",           // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\203\161\203\142\203\166\203\150\203\215\203\142\203\166\212\212\202\350" /* ヒップドロップ滑り */},  // mParent
        "LandRotation",          // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\203\127\203\203\203\223\203\166\217\207\212\212\202\350" /* ジャンプ順滑り */},  // mParent
        "LandRotation",      // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\203\127\203\203\203\223\203\166\213\164\212\212\202\350" /* ジャンプ逆滑り */},  // mParent
        "Fall",              // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\203\130\203\163\203\223\203\161\203\142\203\166\203\150\203\215\203\142\203\166\212\112\216\156" /* スピンヒップドロップ開始 */},  // mParent
        "HipDropHomingStart",          // fileName
        0,                             // animationHash
        0,                             // fileHash
    },
    {
        {"\203\130\203\163\203\223\203\161\203\142\203\166\203\150\203\215\203\142\203\166" /* スピンヒップドロップ */},  // mParent
        "HipDropHoming",           // fileName
        0,                         // animationHash
        0,                         // fileHash
    },
    {
        {"\203\130\203\163\203\223\203\161\203\142\203\166\203\150\203\215\203\142\203\166\222\205\222\156" /* スピンヒップドロップ着地 */},  // mParent
        "HipDropHomingLand",           // fileName
        0,                             // animationHash
        0,                             // fileHash
    },
    {
        {"\203\130\203\212\203\142\203\166\203\101\203\142\203\166" /* スリップアップ */},  // mParent
        "SlipUp",            // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\202\302\202\251\202\334\202\350\203\130\203\212\203\142\203\166\203\101\203\142\203\166\217\200\224\365" /* つかまりスリップアップ準備 */},  // mParent
        "HangSlipUpStart",               // fileName
        0,                               // animationHash
        0,                               // fileHash
    },
    {
        {"\202\302\202\251\202\334\202\350\203\130\203\212\203\142\203\166\203\101\203\142\203\166" /* つかまりスリップアップ */},  // mParent
        "HangSlipUp",                // fileName
        0,                           // animationHash
        0,                           // fileHash
    },
    {
        {"\203\156\203\140\224\362\215\163\222\206" /* ハチ飛行中 */},  // mParent
        "BeeFly",        // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\203\156\203\140\224\362\215\163\222\206\226\263\223\374\227\315" /* ハチ飛行中無入力 */},  // mParent
        "BeeFlyWait",          // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\203\156\203\140\225\307\202\255\202\301\202\302\202\253" /* ハチ壁くっつき */},  // mParent
        "BeeLand",           // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\203\156\203\140\225\307\202\255\202\301\202\302\202\253\222\206" /* ハチ壁くっつき中 */},  // mParent
        "BeeWait",             // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\203\156\203\140\211\324\210\332\223\256" /* ハチ花移動 */},      // mParent
        "BeeCreepWallWalk",  // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\203\156\203\140\203\127\203\203\203\223\203\166" /* ハチジャンプ */},  // mParent
        "BeeJump",         // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\203\156\203\140\225\307\203\127\203\203\203\223\203\166" /* ハチ壁ジャンプ */},  // mParent
        "BeeWallJump",       // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\203\156\203\140\231\263\231\264\203\105\203\107\203\103\203\147" /* ハチ匍匐ウエイト */},  // mParent
        "BeeCreepWait",        // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\203\156\203\140\231\263\231\264\221\117\220\151" /* ハチ匍匐前進 */},  // mParent
        "BeeCreepWalk",    // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\203\156\203\140\231\263\231\264\222\205\222\156" /* ハチ匍匐着地 */},  // mParent
        "BeeCreepLand",    // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\203\156\203\140\203\161\203\142\203\166\203\150\203\215\203\142\203\166\212\112\216\156" /* ハチヒップドロップ開始 */},  // mParent
        "BeeHipDropStart",           // fileName
        0,                           // animationHash
        0,                           // fileHash
    },
    {
        {"\203\156\203\140\203\161\203\142\203\166\203\150\203\215\203\142\203\166" /* ハチヒップドロップ */},  // mParent
        "BeeHipDrop",            // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\203\156\203\140\203\161\203\142\203\166\203\150\203\215\203\142\203\166\222\205\222\156" /* ハチヒップドロップ着地 */},  // mParent
        "BeeHipDropLand",            // fileName
        0,                           // animationHash
        0,                           // fileHash
    },
    {
        {"\203\156\203\140\203\161\203\142\203\166\203\150\203\215\203\142\203\166\225\307\222\205\222\156" /* ハチヒップドロップ壁着地 */},  // mParent
        "BeeHipDropLand",              // fileName
        0,                             // animationHash
        0,                             // fileHash
    },
    {
        {"\223\104\222\341\221\254\225\340\215\163" /* 泥低速歩行 */},  // mParent
        "WalkSoft",      // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\223\104\215\202\221\254\225\340\215\163" /* 泥高速歩行 */},  // mParent
        "WalkBury",      // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\227\216\211\272" /* 落下 */},  // mParent
        "Fall",    // fileName
        0,         // animationHash
        0,         // fileHash
    },
    {
        {"\203\164\201\133\203\164\203\100\203\103\203\136\201\133\224\362\215\163\212\112\216\156" /* フーファイター飛行開始 */},  // mParent
        "FooStart",                  // fileName
        0,                           // animationHash
        0,                           // fileHash
    },
    {
        {"\203\164\201\133\203\164\203\100\203\103\203\136\201\133\224\362\215\163" /* フーファイター飛行 */},  // mParent
        "FooFly",                // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\203\164\201\133\203\164\203\100\203\103\203\136\201\133\224\362\215\163\215\304\212\112" /* フーファイター飛行再開 */},  // mParent
        "FooFlyStart",               // fileName
        0,                           // animationHash
        0,                           // fileHash
    },
    {
        {"\203\164\201\133\203\164\203\100\203\103\203\136\201\133\220\303\216\176" /* フーファイター静止 */},  // mParent
        "FooWait",               // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\203\164\201\133\203\164\203\100\203\103\203\136\201\133\211\360\217\234" /* フーファイター解除 */},  // mParent
        "FooEnd",                // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\203\164\201\133\203\164\203\100\203\103\203\136\201\133\222\205\222\156" /* フーファイター着地 */},  // mParent
        "LandSlope",             // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\203\164\201\133\203\164\203\100\203\103\203\136\201\133\203\130\203\163\203\223" /* フーファイタースピン */},  // mParent
        "FooSpin",                 // fileName
        0,                         // animationHash
        0,                         // fileHash
    },
    {
        {"\225\307\203\127\203\203\203\223\203\166" /* 壁ジャンプ */},  // mParent
        "WallJump",      // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\225\307\202\267\202\327\202\350" /* 壁すべり */},  // mParent
        "WallSlide",   // fileName
        0,             // animationHash
        0,             // fileHash
    },
    {
        {"\225\307\202\255\202\301\202\302\202\253" /* 壁くっつき */},  // mParent
        "WallKeep",      // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\225\307\211\237\202\265" /* 壁押し */},  // mParent
        "Push",      // fileName
        0,           // animationHash
        0,           // fileHash
    },
    {
        {"\225\307\215\266\225\340\202\253" /* 壁左歩き */},  // mParent
        "WallWalkL",   // fileName
        0,             // animationHash
        0,             // fileHash
    },
    {
        {"\225\307\211\105\225\340\202\253" /* 壁右歩き */},  // mParent
        "WallWalkR",   // fileName
        0,             // animationHash
        0,             // fileHash
    },
    {
        {"\225\307\203\105\203\107\203\103\203\147" /* 壁ウエイト */},  // mParent
        "WallWait",      // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\221\117\225\307\203\105\203\107\203\103\203\147" /* 前壁ウエイト */},  // mParent
        "Push",            // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\225\307\203\161\203\142\203\147" /* 壁ヒット */},  // mParent
        "WallHit",     // fileName
        0,             // animationHash
        0,             // fileHash
    },
    {
        {"\225\307\203\161\203\142\203\147\222\205\222\156" /* 壁ヒット着地 */},  // mParent
        "WallHitLand",     // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\225\307\202\315\202\266\202\253" /* 壁はじき */},  // mParent
        "WallHit",     // fileName
        0,             // animationHash
        0,             // fileHash
    },
    {
        {"\212\122\202\323\202\361\202\316\202\350" /* 崖ふんばり */},  // mParent
        "Stagger",       // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\203\112\203\212\203\112\203\212\214\300\212\105" /* カリカリ限界 */},  // mParent
        "WaitHold",        // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\215\342\215\266\203\105\203\107\203\103\203\147" /* 坂左ウエイト */},  // mParent
        "WaitSlopeL",      // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\215\342\211\105\203\105\203\107\203\103\203\147" /* 坂右ウエイト */},  // mParent
        "WaitSlopeR",      // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\215\342\221\117\203\105\203\107\203\103\203\147" /* 坂前ウエイト */},  // mParent
        "WaitSlopeD",      // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\215\342\214\343\203\105\203\107\203\103\203\147" /* 坂後ウエイト */},  // mParent
        "WaitSlopeU",      // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\203\211\203\223" /* ラン */},  // mParent
        "Run",     // fileName
        0,         // animationHash
        0,         // fileHash
    },
    {
        {"\225\340\215\163" /* 歩行 */},  // mParent
        "Walk",    // fileName
        0,         // animationHash
        0,         // fileHash
    },
    {
        {"\223\335\215\163" /* 鈍行 */},    // mParent
        "WalkSoft",  // fileName
        0,           // animationHash
        0,           // fileHash
    },
    {
        {"\203\201\203\136\203\213\203\137\203\142\203\126\203\205" /* メタルダッシュ */},  // mParent
        "RunDash",           // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\226\204\202\334\202\350\225\340\215\163" /* 埋まり歩行 */},  // mParent
        "WalkBury",      // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\226\204\202\334\202\350\203\127\203\203\203\223\203\166A" /* 埋まりジャンプA */},  // mParent
        "WalkBuryJumpLow",    // fileName
        0,                    // animationHash
        0,                    // fileHash
    },
    {
        {"\226\204\202\334\202\350\203\127\203\203\203\223\203\166B" /* 埋まりジャンプB */},  // mParent
        "WalkBuryJumpLow2",   // fileName
        0,                    // animationHash
        0,                    // fileHash
    },
    {
        {"\226\204\202\334\202\350\222\105\217\157\203\127\203\203\203\223\203\166" /* 埋まり脱出ジャンプ */},  // mParent
        "WalkBuryJumpHi",        // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\202\302\202\324\202\352" /* つぶれ */},  // mParent
        "Press",     // fileName
        0,           // animationHash
        0,           // fileHash
    },
    {
        {"\202\302\202\324\202\352\225\234\213\101" /* つぶれ復帰 */},  // mParent
        "PressRecover",  // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\202\302\202\324\202\352\211\360\217\234" /* つぶれ解除 */},  // mParent
        "Jump",          // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\203\165\203\214\201\133\203\114" /* ブレーキ */},  // mParent
        "Brake",       // fileName
        0,             // animationHash
        0,             // fileHash
    },
    {
        {"\203\165\203\214\201\133\203\114\212\212\202\350\217\260" /* ブレーキ滑り床 */},  // mParent
        "Run",               // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\203\136\201\133\203\223\203\165\203\214\201\133\203\114" /* ターンブレーキ */},  // mParent
        "Turn",              // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\203\136\201\133\203\223\203\165\203\214\201\133\203\114\212\212\202\350\217\260" /* ターンブレーキ滑り床 */},  // mParent
        "Run",                     // fileName
        0,                         // animationHash
        0,                         // fileHash
    },
    {
        {"\225\340\215\163\220\247\223\256\203\165\203\214\201\133\203\114" /* 歩行制動ブレーキ */},  // mParent
        "RunEnd",              // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\225\130\217\343\212\265\220\253\221\226\215\163" /* 氷上慣性走行 */},  // mParent
        "Brake",           // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\225\130\217\343\227\315\215\163\215\266" /* 氷上力行左 */},  // mParent
        "SkateL",        // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\225\130\217\343\227\315\215\163\211\105" /* 氷上力行右 */},  // mParent
        "SkateR",        // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\225\130\217\343\220\330\221\326\215\266" /* 氷上切替左 */},  // mParent
        "SkateSwitchL",  // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\225\130\217\343\220\330\221\326\211\105" /* 氷上切替右 */},  // mParent
        "SkateSwitchR",  // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\225\130\217\343\214\343\215\163\215\266" /* 氷上後行左 */},  // mParent
        "SkateBackL",    // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\225\130\217\343\214\343\215\163\211\105" /* 氷上後行右 */},  // mParent
        "SkateBackR",    // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\225\130\217\343\203\136\201\133\203\223" /* 氷上ターン */},  // mParent
        "SkateTurn",     // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\202\265\202\341\202\252\202\335" /* しゃがみ */},  // mParent
        "SquatWait",   // fileName
        0,             // animationHash
        0,             // fileHash
    },
    {
        {"\202\265\202\341\202\252\202\335\212\112\216\156" /* しゃがみ開始 */},  // mParent
        "SquatStart",      // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\202\265\202\341\202\252\202\335\217\111\227\271" /* しゃがみ終了 */},  // mParent
        "SquatEnd",        // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\215\342\202\267\202\327\202\350\217\343\214\374\202\253\202\244\202\302\202\324\202\271" /* 坂すべり上向きうつぶせ */},  // mParent
        "SlideStmach",               // fileName
        0,                           // animationHash
        0,                           // fileHash
    },
    {
        {"\215\342\202\267\202\327\202\350\217\343\214\374\202\253\217\111\227\271" /* 坂すべり上向き終了 */},  // mParent
        "SlideStmachEnd",        // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\215\342\202\267\202\327\202\350\211\272\214\374\202\253\217\111\227\271" /* 坂すべり下向き終了 */},  // mParent
        "SlideHipEnd",           // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\223\301\216\352\203\105\203\107\203\103\203\1471A" /* 特殊ウエイト1A */},  // mParent
        "Sleep",             // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\223\301\216\352\203\105\203\107\203\103\203\1471B" /* 特殊ウエイト1B */},  // mParent
        "SleepLie",          // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\220\355\223\254\203\105\203\107\203\103\203\147" /* 戦闘ウエイト */},  // mParent
        "BattleWait",      // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\213\363\223\135" /* 空転 */},  // mParent
        "Run",     // fileName
        0,         // animationHash
        0,         // fileHash
    },
    {
        {"\202\320\202\353\202\242" /* ひろい */},    // mParent
        "CarryStart",  // fileName
        0,             // animationHash
        0,             // fileHash
    },
    {
        {"\202\320\202\353\202\242\213\363\222\206" /* ひろい空中 */},   // mParent
        "CarryAirStart",  // fileName
        0,                // animationHash
        0,                // fileHash
    },
    {
        {"\202\320\202\353\202\242\203\116\203\103\203\142\203\116" /* ひろいクイック */},  // mParent
        "CarryStartShort",   // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\202\320\202\353\202\242\203\105\203\107\203\103\203\147" /* ひろいウエイト */},  // mParent
        "CarryWait",         // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\203\112\203\165\224\262\202\253" /* カブ抜き */},  // mParent
        "PullOut",     // fileName
        0,             // animationHash
        0,             // fileHash
    },
    {
        {"\203\112\203\165\203\105\203\107\203\103\203\147" /* カブウエイト */},  // mParent
        "PullOutWait",     // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\203\156\203\223\203\175\201\133\223\212\202\260\211\361\223\135\222\206" /* ハンマー投げ回転中 */},  // mParent
        "Swing",                 // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\203\156\203\223\203\175\201\133\223\212\202\260\203\212\203\212\201\133\203\130" /* ハンマー投げリリース */},  // mParent
        "SwingThrow",              // fileName
        0,                         // animationHash
        0,                         // fileHash
    },
    {
        {"\203\122\203\103\203\223\203\121\203\142\203\147" /* コインゲット */},  // mParent
        "CoinGet",         // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\203\160\203\223\203\140" /* パンチ */},  // mParent
        "Spin2nd",   // fileName
        0,           // animationHash
        0,           // fileHash
    },
    {
        {"\213\363\203\160\203\223\203\140" /* 空パンチ */},  // mParent
        "Spin2nd",     // fileName
        0,             // animationHash
        0,             // fileHash
    },
    {
        {"\215\266\203\160\203\223\203\140" /* 左パンチ */},  // mParent
        "Spin2nd",     // fileName
        0,             // animationHash
        0,             // fileHash
    },
    {
        {"\215\266\213\363\203\160\203\223\203\140" /* 左空パンチ */},  // mParent
        "Spin2nd",       // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\203\211\203\223\203\152\203\223\203\117\203\114\203\142\203\116" /* ランニングキック */},  // mParent
        "Kick",                // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\203\101\203\142\203\160\201\133\203\160\203\223\203\140" /* アッパーパンチ */},  // mParent
        "Jump",              // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\202\265\202\341\202\252\202\335\203\101\203\142\203\160\201\133" /* しゃがみアッパー */},  // mParent
        "SquatEnd",            // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\203\124\203\175\201\133\203\134\203\213\203\147" /* サマーソルト */},  // mParent
        "SpinLow",         // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\223\212\202\260" /* 投げ */},  // mParent
        "Throw",   // fileName
        0,         // animationHash
        0,         // fileHash
    },
    {
        {"\203\164\203\100\203\103\203\101\223\212\202\260" /* ファイア投げ */},  // mParent
        "Throw",           // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\227\274\216\350\223\212\202\260" /* 両手投げ */},  // mParent
        "ThrowBoth",   // fileName
        0,             // animationHash
        0,             // fileHash
    },
    {
        {"\203\164\203\100\203\103\203\101\203\130\203\163\203\223" /* ファイアスピン */},  // mParent
        "FireSpin",          // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\203\164\203\100\203\103\203\101\203\130\203\163\203\223\213\363\222\206" /* ファイアスピン空中 */},  // mParent
        "FireSpin",              // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\212\122\202\302\202\251\202\334\202\350\212\112\216\156" /* 崖つかまり開始 */},  // mParent
        "HangStartUnder",    // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\212\122\202\302\202\251\202\334\202\350\222\206" /* 崖つかまり中 */},  // mParent
        "HangWait",        // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\212\122\202\302\202\251\202\334\202\350\217\111\227\271" /* 崖つかまり終了 */},  // mParent
        "HangUp",            // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\212\122\202\302\202\251\202\334\202\350\217\111\227\271\215\342" /* 崖つかまり終了坂 */},  // mParent
        "HangUp",              // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\212\122\202\302\202\251\202\334\202\350\215\176\202\350" /* 崖つかまり降り */},  // mParent
        "HangStart",         // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\202\265\202\341\202\252\202\335\203\127\203\203\203\223\203\166" /* しゃがみジャンプ */},  // mParent
        "JumpBack",            // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\202\265\202\341\202\252\202\335\203\127\203\203\203\223\203\166\222\205\222\156" /* しゃがみジャンプ着地 */},  // mParent
        "JumpBackLand",            // fileName
        0,                         // animationHash
        0,                         // fileHash
    },
    {
        {"\202\265\202\341\202\252\202\335\225\340\202\253" /* しゃがみ歩き */},  // mParent
        "SquatWalk",       // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\202\252\202\361\202\316\202\350\221\226\202\350" /* がんばり走り */},  // mParent
        "RunSlope",        // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\225\240\202\316\202\242\203\127\203\203\203\223\203\166" /* 腹ばいジャンプ */},     // mParent
        "SlideStomachRecover",  // fileName
        0,                      // animationHash
        0,                      // fileHash
    },
    {
        {"\220\113\212\212\202\350\203\127\203\203\203\223\203\166" /* 尻滑りジャンプ */},  // mParent
        "SlideHipRecover",   // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\214\374\202\251\202\242\225\227\221\226\202\350" /* 向かい風走り */},  // mParent
        "RunSlope",        // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\214\374\202\251\202\242\225\227\202\323\202\361\202\316\202\350" /* 向かい風ふんばり */},  // mParent
        "Stagger",             // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\202\273\202\314\217\352\221\253\223\245\202\335\217\343\224\274\220\147" /* その場足踏み上半身 */},  // mParent
        "Walk",                  // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\220\205\211\152\203\105\203\107\203\103\203\147" /* 水泳ウエイト */},  // mParent
        "SwimWait",        // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\220\205\211\152\210\352\221\176\202\253" /* 水泳一掻き */},  // mParent
        "SwimBreast",    // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\220\205\217\343\210\352\221\176\202\253" /* 水上一掻き */},       // mParent
        "SwimBreastSurface",  // fileName
        0,                    // animationHash
        0,                    // fileHash
    },
    {
        {"\220\205\211\152\220\366\202\350" /* 水泳潜り */},  // mParent
        "SwimDive",    // fileName
        0,             // animationHash
        0,             // fileHash
    },
    {
        {"\220\205\211\152\203\127\203\203\203\223\203\166\203\137\203\103\203\165" /* 水泳ジャンプダイブ */},  // mParent
        "LandWater",             // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\220\205\211\152\203\127\203\203\203\223\203\166\203\137\203\103\203\165\211\361\223\135" /* 水泳ジャンプダイブ回転 */},  // mParent
        "LandWaterDive",             // fileName
        0,                           // animationHash
        0,                           // fileHash
    },
    {
        {"\220\205\211\152\217\343\217\270\214\304\213\172" /* 水泳上昇呼吸 */},  // mParent
        "SwimRise",        // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\220\205\211\152\220\205\226\312\217\211\212\372\210\332\223\256" /* 水泳水面初期移動 */},  // mParent
        "SwimStartSurface",    // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\220\205\217\343\203\130\203\163\203\223" /* 水上スピン */},     // mParent
        "SwimSpinSurface",  // fileName
        0,                  // animationHash
        0,                  // fileHash
    },
    {
        {"\220\205\211\152\203\130\203\163\203\223" /* 水泳スピン */},  // mParent
        "SwimSpin",      // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\220\205\211\152\203\130\203\163\203\223\210\332\223\256" /* 水泳スピン移動 */},  // mParent
        "SwimSpinAttack",    // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\220\205\217\343\203\130\203\163\203\223\210\332\223\256" /* 水上スピン移動 */},       // mParent
        "SwimSpinAttackSurface",  // fileName
        0,                        // animationHash
        0,                        // fileHash
    },
    {
        {"\220\205\211\152\203\130\203\163\203\223\203\127\203\203\203\223\203\166" /* 水泳スピンジャンプ */},  // mParent
        "SwimJump",              // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\220\205\211\152\203\137\203\201\201\133\203\127" /* 水泳ダメージ */},   // mParent
        "SwimDamageSmall",  // fileName
        0,                  // animationHash
        0,                  // fileHash
    },
    {
        {"\220\205\211\152\203\137\203\201\201\133\203\127\213\124" /* 水泳ダメージ亀 */},             // mParent
        "SwimFlutterBoardDamageSmall",  // fileName
        0,                              // animationHash
        0,                              // fileHash
    },
    {
        {"\220\205\211\152\203\137\203\201\201\133\203\127\222\206" /* 水泳ダメージ中 */},  // mParent
        "SwimDamageMiddle",  // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\220\205\217\343\203\137\203\201\201\133\203\127\222\206" /* 水上ダメージ中 */},         // mParent
        "SwimDamageMiddleSurface",  // fileName
        0,                          // animationHash
        0,                          // fileHash
    },
    {
        {"\220\205\217\343\203\137\203\201\201\133\203\127\222\205\220\205" /* 水上ダメージ着水 */},           // mParent
        "SwimDamageMiddleSurfaceLand",  // fileName
        0,                              // animationHash
        0,                              // fileHash
    },
    {
        {"\220\205\211\152\203\127\203\106\203\142\203\147" /* 水泳ジェット */},    // mParent
        "SwimFlutterboard",  // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\220\205\211\152\203\127\203\106\203\142\203\147\217\111\227\271" /* 水泳ジェット終了 */},  // mParent
        "SwimDamageMiddle",    // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\220\205\211\152\213\124\223\212\202\260" /* 水泳亀投げ */},           // mParent
        "SwimFlutterboardThrow",  // fileName
        0,                        // animationHash
        0,                        // fileHash
    },
    {
        {"\220\205\211\152\203\127\203\106\203\142\203\147\212\112\216\156" /* 水泳ジェット開始 */},     // mParent
        "SwimFlutterboardStart",  // fileName
        0,                        // animationHash
        0,                        // fileHash
    },
    {
        {"\220\205\211\152\203\127\203\106\203\142\203\147\225\307\203\136\201\133\203\223" /* 水泳ジェット壁ターン */},  // mParent
        "SwimFlutterboardTurn",    // fileName
        0,                         // animationHash
        0,                         // fileHash
    },
    {
        {"\220\205\211\152\203\137\203\105\203\223" /* 水泳ダウン */},  // mParent
        "SwimDie",       // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\220\205\211\152\203\136\201\133\203\223\215\266" /* 水泳ターン左 */},  // mParent
        "SwimTurnL",       // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\220\205\211\152\203\136\201\133\203\223\211\105" /* 水泳ターン右 */},  // mParent
        "SwimTurnR",       // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\220\205\211\152\225\307\203\161\203\142\203\147" /* 水泳壁ヒット */},  // mParent
        "SwimWallHit",     // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\220\205\211\152\203\136\201\133\203\223\211\272" /* 水泳ターン下 */},   // mParent
        "SwimTurnForward",  // fileName
        0,                  // animationHash
        0,                  // fileHash
    },
    {
        {"\220\205\211\152\227\244\202\244\202\277\202\240\202\260" /* 水泳陸うちあげ */},  // mParent
        "SlideStmachEnd",    // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\224\362\202\321\215\236\202\335\217\200\224\365" /* 飛び込み準備 */},  // mParent
        "DiveWait",        // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\224\362\202\321\215\236\202\335\203\127\203\203\203\223\203\166" /* 飛び込みジャンプ */},  // mParent
        "Dive",                // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\214\343\225\373\224\362\202\321\215\236\202\335\203\127\203\203\203\223\203\166" /* 後方飛び込みジャンプ */},  // mParent
        "DiveBack",                // fileName
        0,                         // animationHash
        0,                         // fileHash
    },
    {
        {"\224\362\202\321\215\236\202\335\216\270\224\163\222\205\222\156" /* 飛び込み失敗着地 */},  // mParent
        "Land",                // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\224\362\202\321\215\236\202\335\216\270\224\163\211\361\223\135\222\205\222\156" /* 飛び込み失敗回転着地 */},  // mParent
        "LandRotation",            // fileName
        0,                         // animationHash
        0,                         // fileHash
    },
    {
        {"\203\212\203\223\203\117\203\137\203\142\203\126\203\205" /* リングダッシュ */},  // mParent
        "SwimDashRing",      // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\203\212\203\223\203\117\203\137\203\142\203\126\203\205\217\200\224\365" /* リングダッシュ準備 */},  // mParent
        "SwimDashRingStart",     // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\203\112\203\201\216\235\202\277\203\212\203\223\203\117\217\200\224\365" /* カメ持ちリング準備 */},           // mParent
        "SwimFlutterBoardDashRingStart",  // fileName
        0,                                // animationHash
        0,                                // fileHash
    },
    {
        {"\203\112\203\201\216\235\202\277\203\212\203\223\203\117" /* カメ持ちリング */},          // mParent
        "SwimFlutterBoardDashRing",  // fileName
        0,                           // animationHash
        0,                           // fileHash
    },
    {
        {"\231\263\231\264\221\117\220\151" /* 匍匐前進 */},  // mParent
        "SwimGetUp",   // fileName
        0,             // animationHash
        0,             // fileHash
    },
    {
        {"\221\117\225\373\217\254\203\137\203\201\201\133\203\127" /* 前方小ダメージ */},  // mParent
        "DamageSmallFront",  // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\214\343\225\373\217\254\203\137\203\201\201\133\203\127" /* 後方小ダメージ */},  // mParent
        "DamageSmallBack",   // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\213\255\220\247\203\127\203\203\203\223\203\166" /* 強制ジャンプ */},  // mParent
        "Rise",            // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\222\206\203\137\203\201\201\133\203\127" /* 中ダメージ */},       // mParent
        "DamageMiddleFront",  // fileName
        0,                    // animationHash
        0,                    // fileHash
    },
    {
        {"\222\206\203\137\203\201\201\133\203\127\213\363\222\206" /* 中ダメージ空中 */},      // mParent
        "DamageMiddleFrontAir",  // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\222\206\203\137\203\201\201\133\203\127\222\205\222\156" /* 中ダメージ着地 */},       // mParent
        "DamageMiddleFrontLand",  // fileName
        0,                        // animationHash
        0,                        // fileHash
    },
    {
        {"\222\206\214\343\203\137\203\201\201\133\203\127" /* 中後ダメージ */},    // mParent
        "DamageMiddleBack",  // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\222\206\214\343\203\137\203\201\201\133\203\127\213\363\222\206" /* 中後ダメージ空中 */},   // mParent
        "DamageMiddleBackAir",  // fileName
        0,                      // animationHash
        0,                      // fileHash
    },
    {
        {"\222\206\214\343\203\137\203\201\201\133\203\127\222\205\222\156" /* 中後ダメージ着地 */},    // mParent
        "DamageMiddleBackLand",  // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\211\212\203\137\203\201\201\133\203\127" /* 炎ダメージ */},  // mParent
        "DamageFire",    // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\203\155\201\133\203\137\203\201\201\133\203\127" /* ノーダメージ */},  // mParent
        "DamageBit",       // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\211\212\202\314\203\211\203\223\203\151\201\133" /* 炎のランナー */},  // mParent
        "FireRun",         // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\203\164\203\100\203\103\203\101\203\211\203\223\221\117\222\233" /* ファイアラン前兆 */},  // mParent
        "FireRunStart",        // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\223\144\213\103\203\137\203\201\201\133\203\127" /* 電気ダメージ */},  // mParent
        "DamageElectric",  // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\223\144\213\103\203\137\203\201\201\133\203\127\217\111\227\271" /* 電気ダメージ終了 */},  // mParent
        "DamageElectricEnd",   // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\203\164\203\100\203\103\203\101\203\137\203\223\203\130" /* ファイアダンス */},  // mParent
        "FireRun",           // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\203\137\203\201\201\133\203\127" /* ダメージ */},   // mParent
        "DamageStart",  // fileName
        0,              // animationHash
        0,              // fileHash
    },
    {
        {"\203\137\203\201\201\133\203\127\222\205\222\156" /* ダメージ着地 */},  // mParent
        "Land",            // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\203\137\203\201\201\133\203\127\203\105\203\107\203\103\203\147" /* ダメージウエイト */},  // mParent
        "DamageWait",          // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\203\155\201\133\203\175\203\213\203\105\203\107\203\103\203\147" /* ノーマルウエイト */},  // mParent
        "Wait",                // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\225\130\214\213" /* 氷結 */},  // mParent
        "Freeze",  // fileName
        0,         // animationHash
        0,         // fileHash
    },
    {
        {"\225\130\214\213\211\360\217\234" /* 氷結解除 */},  // mParent
        "IceFlick",    // fileName
        0,             // animationHash
        0,             // fileHash
    },
    {
        {"\202\265\202\321\202\352" /* しびれ */},    // mParent
        "DamageNumb",  // fileName
        0,             // animationHash
        0,             // fileHash
    },
    {
        {"\202\265\202\321\202\352\211\361\225\234" /* しびれ回復 */},   // mParent
        "DamageNumbEnd",  // fileName
        0,                // animationHash
        0,                // fileHash
    },
    {
        {"\202\315\202\313\202\306\202\316\202\263\202\352" /* はねとばされ */},  // mParent
        "DamageFlick",     // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\202\315\202\313\202\306\202\316\202\263\202\352\217\111\227\271" /* はねとばされ終了 */},  // mParent
        "DamageFlickEnd",      // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\214\343\223\135\202\323\202\301\202\306\202\321" /* 後転ふっとび */},   // mParent
        "DamageWeakFront",  // fileName
        0,                  // animationHash
        0,                  // fileHash
    },
    {
        {"\214\343\223\135\202\323\202\301\202\306\202\321\213\363\222\206" /* 後転ふっとび空中 */},  // mParent
        "DamageWeakFrontAir",  // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\214\343\223\135\202\323\202\301\202\306\202\321\222\205\222\156" /* 後転ふっとび着地 */},   // mParent
        "DamageWeakFrontLand",  // fileName
        0,                      // animationHash
        0,                      // fileHash
    },
    {
        {"\215\300\202\350\203\137\203\105\203\223" /* 座りダウン */},  // mParent
        "DieSit",        // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\212\264\223\144\203\137\203\105\203\223" /* 感電ダウン */},  // mParent
        "DieElectric",   // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\211\212\203\137\203\105\203\223" /* 炎ダウン */},  // mParent
        "DieFire",     // fileName
        0,             // animationHash
        0,             // fileHash
    },
    {
        {"\213\302\214\374\202\257\203\137\203\105\203\223" /* 仰向けダウン */},  // mParent
        "DieOver",         // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\230\353\202\271\203\137\203\105\203\223" /* 俯せダウン */},  // mParent
        "DieUnder",      // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\223\336\227\216\203\137\203\105\203\223" /* 奈落ダウン */},  // mParent
        "DieFall",       // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\203\165\203\211\203\142\203\116\203\172\201\133\203\213\227\216\211\272" /* ブラックホール落下 */},  // mParent
        "DieBlackHole",          // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\202\302\202\324\202\352\203\137\203\105\203\223" /* つぶれダウン */},  // mParent
        "DieSit",          // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\203\121\201\133\203\200\203\111\201\133\203\157\201\133" /* ゲームオーバー */},  // mParent
        "DieOver",           // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\203\214\201\133\203\130\225\211\202\257" /* レース負け */},  // mParent
        "DieEvent",      // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\220\205\222\206\203\214\201\133\203\130\225\211\202\257" /* 水中レース負け */},  // mParent
        "DieSwimEvent",      // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\226\204\202\334\202\350\203\137\203\105\203\223" /* 埋まりダウン */},  // mParent
        "DieBury",         // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\203\145\203\152\203\130\203\126\203\207\203\142\203\147\215\266" /* テニスショット左 */},  // mParent
        "TennisShotL",         // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\203\145\203\152\203\130\203\126\203\207\203\142\203\147\211\105" /* テニスショット右 */},  // mParent
        "TennisShotR",         // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\203\145\203\152\203\130\203\126\203\207\203\142\203\147\222\206" /* テニスショット中 */},  // mParent
        "TennisShotM",         // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\203\145\203\152\203\130\203\126\203\207\203\142\203\147\213\363" /* テニスショット空 */},  // mParent
        "TennisShotAir",       // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\203\145\203\152\203\130\203\105\203\107\203\103\203\147" /* テニスウエイト */},  // mParent
        "TennisWait",        // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\203\107\203\214\203\201\203\223\203\147\203\121\203\142\203\147" /* エレメントゲット */},  // mParent
        "ElementGet",          // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\203\107\203\214\203\201\203\223\203\147\203\121\203\142\203\147\220\332\222\156\222\206" /* エレメントゲット接地中 */},  // mParent
        "ElementGetGround",          // fileName
        0,                           // animationHash
        0,                           // fileHash
    },
    {
        {"\213\363\222\206\202\320\202\313\202\350" /* 空中ひねり */},  // mParent
        "Spin",          // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\222\156\217\343\202\320\202\313\202\350" /* 地上ひねり */},  // mParent
        "SpinGround",    // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\203\101\203\103\203\130\202\320\202\313\202\350" /* アイスひねり */},  // mParent
        "IceSpin",         // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\203\101\203\103\203\130\202\320\202\313\202\350\210\332\223\256" /* アイスひねり移動 */},  // mParent
        "IceSkateSpin",        // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\203\101\203\103\203\130\202\320\202\313\202\350\220\303\216\176" /* アイスひねり静止 */},  // mParent
        "IceSpin",             // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\203\156\203\140\203\130\203\163\203\223\213\363\222\206" /* ハチスピン空中 */},  // mParent
        "BeeSpin",           // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\203\156\203\140\203\130\203\163\203\223" /* ハチスピン */},   // mParent
        "BeeSpinGround",  // fileName
        0,                // animationHash
        0,                // fileHash
    },
    {
        {"\203\101\203\103\203\130\202\320\202\313\202\350\213\363\222\206" /* アイスひねり空中 */},  // mParent
        "IceSpinAir",          // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\203\130\203\120\201\133\203\147\203\101\203\116\203\132\203\213\203\127\203\203\203\223\203\166" /* スケートアクセルジャンプ */},  // mParent
        "IceJump",                     // fileName
        0,                             // animationHash
        0,                             // fileHash
    },
    {
        {"\203\130\203\120\201\133\203\147\203\127\203\203\203\223\203\1662" /* スケートジャンプ2 */},  // mParent
        "IceJump2",             // fileName
        0,                      // animationHash
        0,                      // fileHash
    },
    {
        {"\203\130\203\120\201\133\203\147\203\127\203\203\203\223\203\1663" /* スケートジャンプ3 */},  // mParent
        "IceJump3",             // fileName
        0,                      // animationHash
        0,                      // fileHash
    },
    {
        {"\203\130\203\120\201\133\203\147\222\205\222\156" /* スケート着地 */},  // mParent
        "IceJumpLand",     // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\203\130\203\120\201\133\203\147\220\303\216\176\222\205\222\156" /* スケート静止着地 */},  // mParent
        "IceJumpStopLand",     // fileName
        0,                     // animationHash
        0,                     // fileHash
    },
    {
        {"\203\124\201\133\203\164\203\102\203\223\203\127\203\203\203\223\203\166" /* サーフィンジャンプ */},  // mParent
        "SurfJump",              // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\203\124\201\133\203\164\203\102\203\223\203\156\203\103\203\127\203\203\203\223\203\166" /* サーフィンハイジャンプ */},  // mParent
        "SurfJumpHigh",              // fileName
        0,                           // animationHash
        0,                           // fileHash
    },
    {
        {"\203\124\201\133\203\164\203\102\203\223\222\205\222\156" /* サーフィン着地 */},  // mParent
        "SurfLand",          // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\214\251\202\351" /* 見る */},  // mParent
        "Watch",   // fileName
        0,         // animationHash
        0,         // fileHash
    },
    {
        {"\203\214\201\133\203\130\214\251\202\351" /* レース見る */},  // mParent
        "Watch",         // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\203\130\203\145\201\133\203\127\203\103\203\223A" /* ステージインA */},   // mParent
        "StageStartGround",  // fileName
        0,                   // animationHash
        0,                   // fileHash
    },
    {
        {"\203\130\203\145\201\133\203\127\203\103\203\223B" /* ステージインB */},    // mParent
        "LandScenarioStart",  // fileName
        0,                    // animationHash
        0,                    // fileHash
    },
    {
        {"\203\105\203\110\201\133\203\116\203\103\203\223" /* ウォークイン */},  // mParent
        "GoThrough",       // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2021]" /* スピンゲット[デモ1] */},  // mParent
        "DemoGetPower",           // fileName
        0,                        // animationHash
        0,                        // fileHash
    },
    {
        {"\203\130\203\163\203\223\203\121\203\142\203\147[\211\357\230\1421]" /* スピンゲット[会話1] */},  // mParent
        "DemoGetPower",           // fileName
        0,                        // animationHash
        0,                        // fileHash
    },
    {
        {"\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2022]" /* スピンゲット[デモ2] */},  // mParent
        "DemoGetPower",           // fileName
        0,                        // animationHash
        0,                        // fileHash
    },
    {
        {"\203\130\203\163\203\223\203\121\203\142\203\147[\211\357\230\1422]" /* スピンゲット[会話2] */},  // mParent
        "DemoGetPower",           // fileName
        0,                        // animationHash
        0,                        // fileHash
    },
    {
        {"\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2023]" /* スピンゲット[デモ3] */},  // mParent
        "DemoGetPower",           // fileName
        0,                        // animationHash
        0,                        // fileHash
    },
    {
        {"\203\130\203\163\203\223\203\121\203\142\203\147[\211\357\230\1423]" /* スピンゲット[会話3] */},  // mParent
        "DemoGetPower",           // fileName
        0,                        // animationHash
        0,                        // fileHash
    },
    {
        {"\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2024]" /* スピンゲット[デモ4] */},  // mParent
        "DemoGetPower",           // fileName
        0,                        // animationHash
        0,                        // fileHash
    },
    {
        {"\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2025]" /* スピンゲット[デモ5] */},  // mParent
        "DemoGetPower",           // fileName
        0,                        // animationHash
        0,                        // fileHash
    },
    {
        {"\203\130\203\163\203\223\203\121\203\142\203\147[\211\357\230\1424]" /* スピンゲット[会話4] */},  // mParent
        "DemoGetPower",           // fileName
        0,                        // animationHash
        0,                        // fileHash
    },
    {
        {"\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2026]" /* スピンゲット[デモ6] */},  // mParent
        "DemoGetPower",           // fileName
        0,                        // animationHash
        0,                        // fileHash
    },
    {
        {"\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2027]" /* スピンゲット[デモ7] */},  // mParent
        "DemoGetPower",           // fileName
        0,                        // animationHash
        0,                        // fileHash
    },
    {
        {"\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2028]" /* スピンゲット[デモ8] */},  // mParent
        "DemoGetPower",           // fileName
        0,                        // animationHash
        0,                        // fileHash
    },
    {
        {"\203\214\201\133\203\130\212\112\216\156" /* レース開始 */},  // mParent
        "RaceStart",     // fileName
        0,               // animationHash
        0,               // fileHash
    },
    {
        {"\203\214\201\133\203\130\203\116\203\211\203\105\203\140\203\223\203\117\212\112\216\156" /* レースクラウチング開始 */},  // mParent
        "RaceStartCrouch",           // fileName
        0,                           // animationHash
        0,                           // fileHash
    },
    {
        {"\203\123\201\133\203\130\203\147\203\214\201\133\203\130\212\112\216\156" /* ゴーストレース開始 */},  // mParent
        "RaceStartGhost",        // fileName
        0,                       // animationHash
        0,                       // fileHash
    },
    {
        {"\203\123\201\133\203\130\203\147\217\237\227\230" /* ゴースト勝利 */},  // mParent
        "WinGhost",        // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {"\203\123\201\133\203\130\203\147\217\157\214\273" /* ゴースト出現 */},  // mParent
        "AppearGhost",     // fileName
        0,                 // animationHash
        0,                 // fileHash
    },
    {
        {""},  // mParent
        "",    // fileName
        0,     // animationHash
        0,     // fileHash
    },
};

XanimeGroupInfo marioAnimeTable[] = {
    {
        {"\212\356\226\173" /* 基本 */},        // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\265\202\341\202\252\202\335\212\356\226\173" /* しゃがみ基本 */},  // mParent
        1.00000000000f,    // 0x4
        0x2,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\212\356\226\173" /* 水泳基本 */},    // mParent
        1.00000000000f,  // 0x4
        0x1e,            // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\145\203\152\203\130\212\356\226\173" /* テニス基本 */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\215\342\215\266\211\105\203\105\203\107\203\103\203\147" /* 坂左右ウエイト */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\215\342\221\117\214\343\203\105\203\107\203\103\203\147" /* 坂前後ウエイト */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\215\342\215\266\203\105\203\107\203\103\203\147" /* 坂左ウエイト */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\215\342\211\105\203\105\203\107\203\103\203\147" /* 坂右ウエイト */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\215\342\221\117\203\105\203\107\203\103\203\147" /* 坂前ウエイト */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\215\342\214\343\203\105\203\107\203\103\203\147" /* 坂後ウエイト */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\211\203\223" /* ラン */},        // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\225\340\215\163" /* 歩行 */},        // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\223\335\215\163" /* 鈍行 */},        // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\201\203\136\203\213\203\137\203\142\203\126\203\205" /* メタルダッシュ */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\137\203\142\203\126\203\205\203\127\203\203\203\223\203\166" /* ダッシュジャンプ */},  // mParent
        1.00000000000f,        // 0x4
        0x6,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\224\362\202\321\202\267\202\263\202\350\203\127\203\203\203\223\203\166" /* 飛びすさりジャンプ */},  // mParent
        1.00000000000f,          // 0x4
        0x6,                     // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\126\203\207\201\133\203\147\203\127\203\203\203\223\203\166" /* ショートジャンプ */},  // mParent
        1.00000000000f,        // 0x4
        0x6,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\226\204\202\334\202\350\225\340\215\163" /* 埋まり歩行 */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\226\204\202\334\202\350\203\127\203\203\203\223\203\166A" /* 埋まりジャンプA */},  // mParent
        1.00000000000f,       // 0x4
        0x1,                  // 0x8
        0.00000000000f,       // 0xC
        0.00000000000f,       // 0x10
        0.00000000000f,       // 0x14
        0,                    // 0x18
        0,                    // 0x1C
        0,                    // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\226\204\202\334\202\350\203\127\203\203\203\223\203\166B" /* 埋まりジャンプB */},  // mParent
        1.00000000000f,       // 0x4
        0x1,                  // 0x8
        0.00000000000f,       // 0xC
        0.00000000000f,       // 0x10
        0.00000000000f,       // 0x14
        0,                    // 0x18
        0,                    // 0x1C
        0,                    // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\226\204\202\334\202\350\222\105\217\157\203\127\203\203\203\223\203\166" /* 埋まり脱出ジャンプ */},  // mParent
        1.00000000000f,          // 0x4
        0x1,                     // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\213\164\222\205\222\156" /* 逆着地 */},      // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\265\202\341\202\252\202\335" /* しゃがみ */},    // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\165\203\214\201\133\203\114" /* ブレーキ */},    // mParent
        1.00000000000f,  // 0x4
        0x4,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\165\203\214\201\133\203\114\212\212\202\350\217\260" /* ブレーキ滑り床 */},  // mParent
        4.00000000000f,      // 0x4
        0x4,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\136\201\133\203\223\203\165\203\214\201\133\203\114" /* ターンブレーキ */},  // mParent
        1.00000000000f,      // 0x4
        0,                   // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\136\201\133\203\223\203\165\203\214\201\133\203\114\212\212\202\350\217\260" /* ターンブレーキ滑り床 */},  // mParent
        3.00000000000f,            // 0x4
        0x4,                       // 0x8
        0.00000000000f,            // 0xC
        0.00000000000f,            // 0x10
        0.00000000000f,            // 0x14
        0,                         // 0x18
        0,                         // 0x1C
        0,                         // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\225\340\215\163\220\247\223\256\203\165\203\214\201\133\203\114" /* 歩行制動ブレーキ */},  // mParent
        1.00000000000f,        // 0x4
        0x4,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\225\130\217\343\212\265\220\253\221\226\215\163" /* 氷上慣性走行 */},  // mParent
        1.00000000000f,    // 0x4
        0x10,              // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\225\130\217\343\227\315\215\163\215\266" /* 氷上力行左 */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\225\130\217\343\227\315\215\163\211\105" /* 氷上力行右 */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\225\130\217\343\220\330\221\326\215\266" /* 氷上切替左 */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\225\130\217\343\220\330\221\326\211\105" /* 氷上切替右 */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\225\130\217\343\214\343\215\163\215\266" /* 氷上後行左 */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\225\130\217\343\214\343\215\163\211\105" /* 氷上後行右 */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\225\130\217\343\203\136\201\133\203\223" /* 氷上ターン */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\223\301\216\352\203\105\203\107\203\103\203\1471A" /* 特殊ウエイト1A */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\223\301\216\352\203\105\203\107\203\103\203\1471B" /* 特殊ウエイト1B */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\355\223\254\203\105\203\107\203\103\203\147" /* 戦闘ウエイト */},  // mParent
        1.00000000000f,    // 0x4
        0x1e,              // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\231\263\231\264\221\117\220\151" /* 匍匐前進 */},    // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\127\203\203\203\223\203\166" /* ジャンプ */},    // mParent
        1.00000000000f,  // 0x4
        0,               // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\127\203\203\203\223\203\166B" /* ジャンプB */},   // mParent
        1.00000000000f,  // 0x4
        0,               // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\127\203\203\203\223\203\166C" /* ジャンプC */},   // mParent
        1.00000000000f,  // 0x4
        0,               // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\136\201\133\203\223\203\127\203\203\203\223\203\166" /* ターンジャンプ */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\164\203\212\201\133\203\127\203\203\203\223\203\166" /* フリージャンプ */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\127\203\203\203\223\203\166\202\323\202\3351" /* ジャンプふみ1 */},  // mParent
        1.00000000000f,     // 0x4
        0x6,                // 0x8
        0.00000000000f,     // 0xC
        0.00000000000f,     // 0x10
        0.00000000000f,     // 0x14
        0,                  // 0x18
        0,                  // 0x1C
        0,                  // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\127\203\203\203\223\203\166\202\323\202\3352" /* ジャンプふみ2 */},  // mParent
        1.00000000000f,     // 0x4
        0x6,                // 0x8
        0.00000000000f,     // 0xC
        0.00000000000f,     // 0x10
        0.00000000000f,     // 0x14
        0,                  // 0x18
        0,                  // 0x1C
        0,                  // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\127\203\203\203\223\203\166\202\323\202\3353" /* ジャンプふみ3 */},  // mParent
        1.00000000000f,     // 0x4
        0x6,                // 0x8
        0.00000000000f,     // 0xC
        0.00000000000f,     // 0x10
        0.00000000000f,     // 0x14
        0,                  // 0x18
        0,                  // 0x1C
        0,                  // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\210\370\202\253\226\337\202\265" /* 引き戻し */},    // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\210\370\202\253\226\337\202\265\222\205\222\156" /* 引き戻し着地 */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\174\203\142\203\150\203\217\201\133\203\166\212\112\216\156" /* ポッドワープ開始 */},  // mParent
        1.00000000000f,        // 0x4
        0x6,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\174\203\142\203\150\203\217\201\133\203\166\217\111\227\271" /* ポッドワープ終了 */},  // mParent
        1.00000000000f,        // 0x4
        0x6,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\112\203\103\203\211\203\165\203\127\203\203\203\223\203\166" /* スカイラブジャンプ */},  // mParent
        1.00000000000f,          // 0x4
        0,                       // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\213\363\222\206\210\352\211\361\223\135" /* 空中一回転 */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\120\203\114\203\210" /* スケキヨ */},    // mParent
        1.00000000000f,  // 0x4
        0x1,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\120\203\114\203\210\222\105\217\157" /* スケキヨ脱出 */},  // mParent
        1.00000000000f,    // 0x4
        0x1,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\226\204\202\334\202\350" /* 埋まり */},      // mParent
        1.00000000000f,  // 0x4
        0x1,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\226\204\202\334\202\350\222\105\217\157" /* 埋まり脱出 */},  // mParent
        1.00000000000f,  // 0x4
        0x1,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\172\203\142\203\160\201\133\203\127\203\203\203\223\203\166A" /* ホッパージャンプA */},  // mParent
        1.00000000000f,         // 0x4
        0x6,                    // 0x8
        0.00000000000f,         // 0xC
        0.00000000000f,         // 0x10
        0.00000000000f,         // 0x14
        0,                      // 0x18
        0,                      // 0x1C
        0,                      // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\172\203\142\203\160\201\133\203\127\203\203\203\223\203\166B" /* ホッパージャンプB */},  // mParent
        1.00000000000f,         // 0x4
        0x6,                    // 0x8
        0.00000000000f,         // 0xC
        0.00000000000f,         // 0x10
        0.00000000000f,         // 0x14
        0,                      // 0x18
        0,                      // 0x1C
        0,                      // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\172\203\142\203\160\201\133\202\323\202\335\203\127\203\203\203\223\203\166A" /* ホッパーふみジャンプA */},  // mParent
        1.00000000000f,             // 0x4
        0x6,                        // 0x8
        0.00000000000f,             // 0xC
        0.00000000000f,             // 0x10
        0.00000000000f,             // 0x14
        0,                          // 0x18
        0,                          // 0x1C
        0,                          // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\172\203\142\203\160\201\133\202\323\202\335\203\127\203\203\203\223\203\166B" /* ホッパーふみジャンプB */},  // mParent
        1.00000000000f,             // 0x4
        0x6,                        // 0x8
        0.00000000000f,             // 0xC
        0.00000000000f,             // 0x10
        0.00000000000f,             // 0x14
        0,                          // 0x18
        0,                          // 0x1C
        0,                          // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\172\203\142\203\160\201\133\210\332\223\256A" /* ホッパー移動A */},  // mParent
        1.00000000000f,     // 0x4
        0x6,                // 0x8
        0.00000000000f,     // 0xC
        0.00000000000f,     // 0x10
        0.00000000000f,     // 0x14
        0,                  // 0x18
        0,                  // 0x1C
        0,                  // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\172\203\142\203\160\201\133\210\332\223\256B" /* ホッパー移動B */},  // mParent
        1.00000000000f,     // 0x4
        0x6,                // 0x8
        0.00000000000f,     // 0xC
        0.00000000000f,     // 0x10
        0.00000000000f,     // 0x14
        0,                  // 0x18
        0,                  // 0x1C
        0,                  // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\172\203\142\203\160\201\133\203\156\203\103\203\127\203\203\203\223\203\166A" /* ホッパーハイジャンプA */},  // mParent
        1.00000000000f,             // 0x4
        0x1,                        // 0x8
        0.00000000000f,             // 0xC
        0.00000000000f,             // 0x10
        0.00000000000f,             // 0x14
        0,                          // 0x18
        0,                          // 0x1C
        0,                          // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\172\203\142\203\160\201\133\203\156\203\103\203\127\203\203\203\223\203\166B" /* ホッパーハイジャンプB */},  // mParent
        1.00000000000f,             // 0x4
        0x1,                        // 0x8
        0.00000000000f,             // 0xC
        0.00000000000f,             // 0x10
        0.00000000000f,             // 0x14
        0,                          // 0x18
        0,                          // 0x1C
        0,                          // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\172\203\142\203\160\201\133\225\307\203\127\203\203\203\223\203\166" /* ホッパー壁ジャンプ */},  // mParent
        1.00000000000f,          // 0x4
        0x6,                     // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\172\203\142\203\160\201\133\203\161\203\142\203\166\203\150\203\215\203\142\203\166\212\112\216\156" /* ホッパーヒップドロップ開始 */},  // mParent
        1.00000000000f,                  // 0x4
        0x6,                             // 0x8
        0.00000000000f,                  // 0xC
        0.00000000000f,                  // 0x10
        0.00000000000f,                  // 0x14
        0,                               // 0x18
        0,                               // 0x1C
        0,                               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\172\203\142\203\160\201\133\203\161\203\142\203\166\203\150\203\215\203\142\203\166" /* ホッパーヒップドロップ */},  // mParent
        1.00000000000f,              // 0x4
        0x6,                         // 0x8
        0.00000000000f,              // 0xC
        0.00000000000f,              // 0x10
        0.00000000000f,              // 0x14
        0,                           // 0x18
        0,                           // 0x1C
        0,                           // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\225\235\202\306\202\321" /* 幅とび */},      // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\161\203\142\203\166\203\150\203\215\203\142\203\166\212\112\216\156" /* ヒップドロップ開始 */},  // mParent
        1.00000000000f,          // 0x4
        0x2,                     // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\161\203\142\203\166\203\150\203\215\203\142\203\166" /* ヒップドロップ */},  // mParent
        1.00000000000f,      // 0x4
        0,                   // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\161\203\142\203\166\203\150\203\215\203\142\203\166\222\205\222\156" /* ヒップドロップ着地 */},  // mParent
        1.00000000000f,          // 0x4
        0x6,                     // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\161\203\142\203\166\203\150\203\215\203\142\203\166\212\212\202\350" /* ヒップドロップ滑り */},  // mParent
        1.00000000000f,          // 0x4
        0x6,                     // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\127\203\203\203\223\203\166\217\207\212\212\202\350" /* ジャンプ順滑り */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\127\203\203\203\223\203\166\213\164\212\212\202\350" /* ジャンプ逆滑り */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\212\203\142\203\166\203\101\203\142\203\166" /* スリップアップ */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\302\202\251\202\334\202\350\203\130\203\212\203\142\203\166\203\101\203\142\203\166\217\200\224\365" /* つかまりスリップアップ準備 */},  // mParent
        1.00000000000f,                  // 0x4
        0x4,                             // 0x8
        0.00000000000f,                  // 0xC
        0.00000000000f,                  // 0x10
        0.00000000000f,                  // 0x14
        0,                               // 0x18
        0,                               // 0x1C
        0,                               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\302\202\251\202\334\202\350\203\130\203\212\203\142\203\166\203\101\203\142\203\166" /* つかまりスリップアップ */},  // mParent
        1.00000000000f,              // 0x4
        0x2,                         // 0x8
        0.00000000000f,              // 0xC
        0.00000000000f,              // 0x10
        0.00000000000f,              // 0x14
        0,                           // 0x18
        0,                           // 0x1C
        0,                           // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\163\203\223\203\161\203\142\203\166\203\150\203\215\203\142\203\166\212\112\216\156" /* スピンヒップドロップ開始 */},  // mParent
        1.00000000000f,                // 0x4
        0x2,                           // 0x8
        0.00000000000f,                // 0xC
        0.00000000000f,                // 0x10
        0.00000000000f,                // 0x14
        0,                             // 0x18
        0,                             // 0x1C
        0,                             // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\163\203\223\203\161\203\142\203\166\203\150\203\215\203\142\203\166" /* スピンヒップドロップ */},  // mParent
        1.00000000000f,            // 0x4
        0,                         // 0x8
        0.00000000000f,            // 0xC
        0.00000000000f,            // 0x10
        0.00000000000f,            // 0x14
        0,                         // 0x18
        0,                         // 0x1C
        0,                         // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\163\203\223\203\161\203\142\203\166\203\150\203\215\203\142\203\166\222\205\222\156" /* スピンヒップドロップ着地 */},  // mParent
        1.00000000000f,                // 0x4
        0x6,                           // 0x8
        0.00000000000f,                // 0xC
        0.00000000000f,                // 0x10
        0.00000000000f,                // 0x14
        0,                             // 0x18
        0,                             // 0x1C
        0,                             // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\227\216\211\272" /* 落下 */},        // mParent
        1.00000000000f,  // 0x4
        0xf,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\222\205\222\156" /* 着地 */},        // mParent
        1.00000000000f,  // 0x4
        0,               // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\222\205\222\156B" /* 着地B */},       // mParent
        1.00000000000f,  // 0x4
        0,               // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\156\201\133\203\150\222\205\222\156" /* ハード着地 */},  // mParent
        1.00000000000f,  // 0x4
        0,               // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\267\202\327\202\350\222\205\222\156" /* すべり着地 */},  // mParent
        1.00000000000f,  // 0x4
        0,               // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\126\203\207\201\133\203\147\222\205\222\156" /* ショート着地 */},  // mParent
        1.00000000000f,    // 0x4
        0,                 // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\156\203\140\224\362\215\163\222\206" /* ハチ飛行中 */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\156\203\140\224\362\215\163\222\206\226\263\223\374\227\315" /* ハチ飛行中無入力 */},  // mParent
        1.00000000000f,        // 0x4
        0x6,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\156\203\140\225\307\202\255\202\301\202\302\202\253" /* ハチ壁くっつき */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\156\203\140\225\307\202\255\202\301\202\302\202\253\222\206" /* ハチ壁くっつき中 */},  // mParent
        1.00000000000f,        // 0x4
        0x6,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\156\203\140\203\127\203\203\203\223\203\166" /* ハチジャンプ */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\156\203\140\225\307\203\127\203\203\203\223\203\166" /* ハチ壁ジャンプ */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\156\203\140\231\263\231\264\203\105\203\107\203\103\203\147" /* ハチ匍匐ウエイト */},  // mParent
        1.00000000000f,        // 0x4
        0x6,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\156\203\140\231\263\231\264\221\117\220\151" /* ハチ匍匐前進 */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\156\203\140\231\263\231\264\222\205\222\156" /* ハチ匍匐着地 */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\156\203\140\211\324\210\332\223\256" /* ハチ花移動 */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\156\203\140\203\161\203\142\203\166\203\150\203\215\203\142\203\166\212\112\216\156" /* ハチヒップドロップ開始 */},  // mParent
        1.00000000000f,              // 0x4
        0x2,                         // 0x8
        0.00000000000f,              // 0xC
        0.00000000000f,              // 0x10
        0.00000000000f,              // 0x14
        0,                           // 0x18
        0,                           // 0x1C
        0,                           // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\156\203\140\203\161\203\142\203\166\203\150\203\215\203\142\203\166" /* ハチヒップドロップ */},  // mParent
        1.00000000000f,          // 0x4
        0,                       // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\156\203\140\203\161\203\142\203\166\203\150\203\215\203\142\203\166\222\205\222\156" /* ハチヒップドロップ着地 */},  // mParent
        1.00000000000f,              // 0x4
        0x6,                         // 0x8
        0.00000000000f,              // 0xC
        0.00000000000f,              // 0x10
        0.00000000000f,              // 0x14
        0,                           // 0x18
        0,                           // 0x1C
        0,                           // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\156\203\140\203\161\203\142\203\166\203\150\203\215\203\142\203\166\225\307\222\205\222\156" /* ハチヒップドロップ壁着地 */},  // mParent
        1.00000000000f,                // 0x4
        0x6,                           // 0x8
        0.00000000000f,                // 0xC
        0.00000000000f,                // 0x10
        0.00000000000f,                // 0x14
        0,                             // 0x18
        0,                             // 0x1C
        0,                             // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\164\201\133\203\164\203\100\203\103\203\136\201\133\224\362\215\163\212\112\216\156" /* フーファイター飛行開始 */},  // mParent
        1.00000000000f,              // 0x4
        0x6,                         // 0x8
        0.00000000000f,              // 0xC
        0.00000000000f,              // 0x10
        0.00000000000f,              // 0x14
        0,                           // 0x18
        0,                           // 0x1C
        0,                           // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\164\201\133\203\164\203\100\203\103\203\136\201\133\224\362\215\163" /* フーファイター飛行 */},  // mParent
        1.00000000000f,          // 0x4
        0x6,                     // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\164\201\133\203\164\203\100\203\103\203\136\201\133\224\362\215\163\215\304\212\112" /* フーファイター飛行再開 */},  // mParent
        1.00000000000f,              // 0x4
        0x6,                         // 0x8
        0.00000000000f,              // 0xC
        0.00000000000f,              // 0x10
        0.00000000000f,              // 0x14
        0,                           // 0x18
        0,                           // 0x1C
        0,                           // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\164\201\133\203\164\203\100\203\103\203\136\201\133\220\303\216\176" /* フーファイター静止 */},  // mParent
        1.00000000000f,          // 0x4
        0x6,                     // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\164\201\133\203\164\203\100\203\103\203\136\201\133\211\360\217\234" /* フーファイター解除 */},  // mParent
        1.00000000000f,          // 0x4
        0x6,                     // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\164\201\133\203\164\203\100\203\103\203\136\201\133\222\205\222\156" /* フーファイター着地 */},  // mParent
        1.00000000000f,          // 0x4
        0x6,                     // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\164\201\133\203\164\203\100\203\103\203\136\201\133\203\130\203\163\203\223" /* フーファイタースピン */},  // mParent
        1.00000000000f,            // 0x4
        0x6,                       // 0x8
        0.00000000000f,            // 0xC
        0.00000000000f,            // 0x10
        0.00000000000f,            // 0x14
        0,                         // 0x18
        0,                         // 0x1C
        0,                         // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\223\104\222\341\221\254\225\340\215\163" /* 泥低速歩行 */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\223\104\215\202\221\254\225\340\215\163" /* 泥高速歩行 */},  // mParent
        2.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\225\307\202\255\202\301\202\302\202\253" /* 壁くっつき */},  // mParent
        1.00000000000f,  // 0x4
        0,               // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\225\307\202\267\202\327\202\350" /* 壁すべり */},    // mParent
        1.00000000000f,  // 0x4
        0xa,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\225\307\203\127\203\203\203\223\203\166" /* 壁ジャンプ */},  // mParent
        1.00000000000f,  // 0x4
        0,               // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\225\307\211\237\202\265" /* 壁押し */},      // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\225\307\215\266\225\340\202\253" /* 壁左歩き */},    // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\225\307\211\105\225\340\202\253" /* 壁右歩き */},    // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\225\307\203\105\203\107\203\103\203\147" /* 壁ウエイト */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\221\117\225\307\203\105\203\107\203\103\203\147" /* 前壁ウエイト */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\225\307\203\161\203\142\203\147" /* 壁ヒット */},    // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\225\307\203\161\203\142\203\147\222\205\222\156" /* 壁ヒット着地 */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\225\307\202\315\202\266\202\253" /* 壁はじき */},    // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\137\203\201\201\133\203\127" /* ダメージ */},    // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\137\203\201\201\133\203\127\222\205\222\156" /* ダメージ着地 */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\137\203\201\201\133\203\127\203\105\203\107\203\103\203\147" /* ダメージウエイト */},  // mParent
        1.00000000000f,        // 0x4
        0x6,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\155\201\133\203\175\203\213\203\105\203\107\203\103\203\147" /* ノーマルウエイト */},  // mParent
        1.00000000000f,        // 0x4
        0x6,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\225\130\214\213" /* 氷結 */},        // mParent
        1.00000000000f,  // 0x4
        0,               // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\225\130\214\213\211\360\217\234" /* 氷結解除 */},    // mParent
        1.00000000000f,  // 0x4
        0,               // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\265\202\321\202\352" /* しびれ */},      // mParent
        1.00000000000f,  // 0x4
        0,               // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\265\202\321\202\352\211\361\225\234" /* しびれ回復 */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\122\203\103\203\223\203\121\203\142\203\147" /* コインゲット */},  // mParent
        1.00000000000f,    // 0x4
        0x1,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\160\203\223\203\140" /* パンチ */},      // mParent
        1.00000000000f,  // 0x4
        0x1,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\213\363\203\160\203\223\203\140" /* 空パンチ */},    // mParent
        1.00000000000f,  // 0x4
        0x1,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\215\266\203\160\203\223\203\140" /* 左パンチ */},    // mParent
        1.00000000000f,  // 0x4
        0x1,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\215\266\213\363\203\160\203\223\203\140" /* 左空パンチ */},  // mParent
        1.00000000000f,  // 0x4
        0x1,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\211\203\223\203\152\203\223\203\117\203\114\203\142\203\116" /* ランニングキック */},  // mParent
        1.00000000000f,        // 0x4
        0,                     // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\101\203\142\203\160\201\133\203\160\203\223\203\140" /* アッパーパンチ */},  // mParent
        1.00000000000f,      // 0x4
        0x3,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\265\202\341\202\252\202\335\203\101\203\142\203\160\201\133" /* しゃがみアッパー */},  // mParent
        1.00000000000f,        // 0x4
        0x3,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\124\203\175\201\133\203\134\203\213\203\147" /* サマーソルト */},  // mParent
        1.00000000000f,    // 0x4
        0,                 // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\121\201\133\203\200\203\111\201\133\203\157\201\133" /* ゲームオーバー */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\302\202\324\202\352" /* つぶれ */},      // mParent
        0.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\302\202\324\202\352\225\234\213\101" /* つぶれ復帰 */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\302\202\324\202\352\211\360\217\234" /* つぶれ解除 */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\213\363\223\135" /* 空転 */},        // mParent
        4.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\215\342\202\267\202\327\202\350\217\343\214\374\202\253\202\244\202\302\202\324\202\271" /* 坂すべり上向きうつぶせ */},  // mParent
        1.00000000000f,              // 0x4
        0x6,                         // 0x8
        0.00000000000f,              // 0xC
        0.00000000000f,              // 0x10
        0.00000000000f,              // 0x14
        0,                           // 0x18
        0,                           // 0x1C
        0,                           // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\215\342\202\267\202\327\202\350\211\272\214\374\202\253\202\240\202\250\202\336\202\257" /* 坂すべり下向きあおむけ */},  // mParent
        2.00000000000f,              // 0x4
        0x6,                         // 0x8
        0.00000000000f,              // 0xC
        0.00000000000f,              // 0x10
        0.00000000000f,              // 0x14
        0,                           // 0x18
        0,                           // 0x1C
        0,                           // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\214\374\202\251\202\242\225\227\221\226\202\350" /* 向かい風走り */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\214\374\202\251\202\242\225\227\202\323\202\361\202\316\202\350" /* 向かい風ふんばり */},  // mParent
        1.00000000000f,        // 0x4
        0x6,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\124\201\133\203\164\203\102\203\223" /* サーフィン */},  // mParent
        1.00000000000f,  // 0x4
        0xf,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\124\201\133\203\164\203\102\203\223\201\151\211\301\221\254\201\152" /* サーフィン（加速） */},  // mParent
        1.00000000000f,          // 0x4
        0xf,                     // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\124\201\133\203\164\203\102\203\223\214\130\202\253\212\112\216\156" /* サーフィン傾き開始 */},  // mParent
        1.00000000000f,          // 0x4
        0x4,                     // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\124\201\133\203\164\203\102\203\223\214\130\202\253\212\112\216\156\201\151\211\301\221\254\201\152" /* サーフィン傾き開始（加速） */},  // mParent
        1.00000000000f,                  // 0x4
        0x4,                             // 0x8
        0.00000000000f,                  // 0xC
        0.00000000000f,                  // 0x10
        0.00000000000f,                  // 0x14
        0,                               // 0x18
        0,                               // 0x1C
        0,                               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\124\201\133\203\164\203\102\203\223\203\127\203\203\203\223\203\166" /* サーフィンジャンプ */},  // mParent
        1.00000000000f,          // 0x4
        0x1,                     // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\124\201\133\203\164\203\102\203\223\203\156\203\103\203\127\203\203\203\223\203\166" /* サーフィンハイジャンプ */},  // mParent
        1.00000000000f,              // 0x4
        0x6,                         // 0x8
        0.00000000000f,              // 0xC
        0.00000000000f,              // 0x10
        0.00000000000f,              // 0x14
        0,                           // 0x18
        0,                           // 0x1C
        0,                           // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\124\201\133\203\164\203\102\203\223\227\216\211\272" /* サーフィン落下 */},  // mParent
        1.00000000000f,      // 0x4
        0xf,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\124\201\133\203\164\203\102\203\223\222\205\222\156" /* サーフィン着地 */},  // mParent
        1.00000000000f,      // 0x4
        0x1,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\136\203\175\203\122\203\215\210\332\223\256" /* タマコロ移動 */},  // mParent
        1.00000000000f,    // 0x4
        0xf,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\136\203\175\203\122\203\215\202\265\202\341\202\252\202\335" /* タマコロしゃがみ */},  // mParent
        1.00000000000f,        // 0x4
        0x6,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\211\203\103\203\137\201\133\220\113" /* スライダー尻 */},  // mParent
        2.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\273\202\314\217\352\221\253\223\245\202\335" /* その場足踏み */},  // mParent
        1.00000000000f,    // 0x4
        0x2,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\273\202\314\217\352\221\253\223\245\202\335\217\343\224\274\220\147" /* その場足踏み上半身 */},  // mParent
        1.00000000000f,          // 0x4
        0x4,                     // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\210\352\221\176\202\253" /* 水泳一掻き */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\217\343\210\352\221\176\202\253" /* 水上一掻き */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\220\366\202\350" /* 水泳潜り */},    // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\203\127\203\203\203\223\203\166\203\137\203\103\203\165" /* 水泳ジャンプダイブ */},  // mParent
        1.00000000000f,          // 0x4
        0x1,                     // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\203\127\203\203\203\223\203\166\203\137\203\103\203\165\211\361\223\135" /* 水泳ジャンプダイブ回転 */},  // mParent
        1.00000000000f,              // 0x4
        0x1,                         // 0x8
        0.00000000000f,              // 0xC
        0.00000000000f,              // 0x10
        0.00000000000f,              // 0x14
        0,                           // 0x18
        0,                           // 0x1C
        0,                           // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\217\343\217\270\214\304\213\172" /* 水泳上昇呼吸 */},  // mParent
        1.00000000000f,    // 0x4
        0x10,              // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\220\205\226\312\217\211\212\372\210\332\223\256" /* 水泳水面初期移動 */},  // mParent
        1.00000000000f,        // 0x4
        0x6,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\217\343\203\130\203\163\203\223" /* 水上スピン */},  // mParent
        1.00000000000f,  // 0x4
        0x1,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\203\130\203\163\203\223" /* 水泳スピン */},  // mParent
        1.00000000000f,  // 0x4
        0x1,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\203\130\203\163\203\223\210\332\223\256" /* 水泳スピン移動 */},  // mParent
        1.00000000000f,      // 0x4
        0x1,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\217\343\203\130\203\163\203\223\210\332\223\256" /* 水上スピン移動 */},  // mParent
        1.00000000000f,      // 0x4
        0x1,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\203\137\203\201\201\133\203\127" /* 水泳ダメージ */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\203\137\203\201\201\133\203\127\213\124" /* 水泳ダメージ亀 */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\203\137\203\201\201\133\203\127\222\206" /* 水泳ダメージ中 */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\217\343\203\137\203\201\201\133\203\127\222\206" /* 水上ダメージ中 */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\217\343\203\137\203\201\201\133\203\127\222\205\220\205" /* 水上ダメージ着水 */},  // mParent
        1.00000000000f,        // 0x4
        0x6,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\203\137\203\105\203\223" /* 水泳ダウン */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\203\136\201\133\203\223\215\266" /* 水泳ターン左 */},  // mParent
        1.00000000000f,    // 0x4
        0x14,              // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\203\136\201\133\203\223\211\105" /* 水泳ターン右 */},  // mParent
        1.00000000000f,    // 0x4
        0x14,              // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\203\136\201\133\203\223\211\272" /* 水泳ターン下 */},  // mParent
        1.00000000000f,    // 0x4
        0x14,              // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\203\105\203\107\203\103\203\147" /* 水泳ウエイト */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\203\127\203\106\203\142\203\147" /* 水泳ジェット */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\203\127\203\106\203\142\203\147\212\112\216\156" /* 水泳ジェット開始 */},  // mParent
        1.00000000000f,        // 0x4
        0x6,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\203\127\203\106\203\142\203\147\217\111\227\271" /* 水泳ジェット終了 */},  // mParent
        1.00000000000f,        // 0x4
        0x6,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\213\124\223\212\202\260" /* 水泳亀投げ */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\225\307\203\161\203\142\203\147" /* 水泳壁ヒット */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\203\127\203\106\203\142\203\147\225\307\203\136\201\133\203\223" /* 水泳ジェット壁ターン */},  // mParent
        1.00000000000f,            // 0x4
        0x6,                       // 0x8
        0.00000000000f,            // 0xC
        0.00000000000f,            // 0x10
        0.00000000000f,            // 0x14
        0,                         // 0x18
        0,                         // 0x1C
        0,                         // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\227\244\202\244\202\277\202\240\202\260" /* 水泳陸うちあげ */},  // mParent
        1.00000000000f,      // 0x4
        0x10,                // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\224\362\202\321\215\236\202\335\217\200\224\365" /* 飛び込み準備 */},  // mParent
        1.00000000000f,    // 0x4
        0x14,              // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\224\362\202\321\215\236\202\335\203\127\203\203\203\223\203\166" /* 飛び込みジャンプ */},  // mParent
        1.00000000000f,        // 0x4
        0x6,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\214\343\225\373\224\362\202\321\215\236\202\335\203\127\203\203\203\223\203\166" /* 後方飛び込みジャンプ */},  // mParent
        1.00000000000f,            // 0x4
        0x6,                       // 0x8
        0.00000000000f,            // 0xC
        0.00000000000f,            // 0x10
        0.00000000000f,            // 0x14
        0,                         // 0x18
        0,                         // 0x1C
        0,                         // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\224\362\202\321\215\236\202\335\216\270\224\163\222\205\222\156" /* 飛び込み失敗着地 */},  // mParent
        1.00000000000f,        // 0x4
        0xf,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\224\362\202\321\215\236\202\335\216\270\224\163\211\361\223\135\222\205\222\156" /* 飛び込み失敗回転着地 */},  // mParent
        1.00000000000f,            // 0x4
        0x6,                       // 0x8
        0.00000000000f,            // 0xC
        0.00000000000f,            // 0x10
        0.00000000000f,            // 0x14
        0,                         // 0x18
        0,                         // 0x1C
        0,                         // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\211\152\203\130\203\163\203\223\203\127\203\203\203\223\203\166" /* 水泳スピンジャンプ */},  // mParent
        1.00000000000f,          // 0x4
        0x6,                     // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\212\203\223\203\117\203\137\203\142\203\126\203\205" /* リングダッシュ */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\212\203\223\203\117\203\137\203\142\203\126\203\205\217\200\224\365" /* リングダッシュ準備 */},  // mParent
        1.00000000000f,          // 0x4
        0x6,                     // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\112\203\201\216\235\202\277\203\212\203\223\203\117\217\200\224\365" /* カメ持ちリング準備 */},  // mParent
        1.00000000000f,          // 0x4
        0x6,                     // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\112\203\201\216\235\202\277\203\212\203\223\203\117" /* カメ持ちリング */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\222\205\222\156C" /* 着地C */},       // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\222\205\222\156\203\136\201\133\203\223" /* 着地ターン */},  // mParent
        1.00000000000f,  // 0x4
        0,               // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\222\205\222\156\225\235\202\306\202\321" /* 着地幅とび */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\265\202\341\202\252\202\335\212\112\216\156" /* しゃがみ開始 */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\265\202\341\202\252\202\335\217\111\227\271" /* しゃがみ終了 */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\215\342\202\267\202\327\202\350\217\343\214\374\202\253\217\111\227\271" /* 坂すべり上向き終了 */},  // mParent
        1.00000000000f,          // 0x4
        0x4,                     // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\215\342\202\267\202\327\202\350\211\272\214\374\202\253\217\111\227\271" /* 坂すべり下向き終了 */},  // mParent
        1.00000000000f,          // 0x4
        0x4,                     // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\320\202\353\202\242" /* ひろい */},      // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\320\202\353\202\242\213\363\222\206" /* ひろい空中 */},  // mParent
        1.00000000000f,  // 0x4
        0x3,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\320\202\353\202\242\203\116\203\103\203\142\203\116" /* ひろいクイック */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\320\202\353\202\242\203\105\203\107\203\103\203\147" /* ひろいウエイト */},  // mParent
        1.00000000000f,      // 0x4
        0,                   // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\112\203\165\224\262\202\253" /* カブ抜き */},    // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\112\203\165\203\105\203\107\203\103\203\147" /* カブウエイト */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\156\203\223\203\175\201\133\223\212\202\260\211\361\223\135\222\206" /* ハンマー投げ回転中 */},  // mParent
        1.00000000000f,          // 0x4
        0x6,                     // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\156\203\223\203\175\201\133\223\212\202\260\203\212\203\212\201\133\203\130" /* ハンマー投げリリース */},  // mParent
        1.00000000000f,            // 0x4
        0x6,                       // 0x8
        0.00000000000f,            // 0xC
        0.00000000000f,            // 0x10
        0.00000000000f,            // 0x14
        0,                         // 0x18
        0,                         // 0x1C
        0,                         // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\223\212\202\260" /* 投げ */},        // mParent
        1.00000000000f,  // 0x4
        0,               // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\164\203\100\203\103\203\101\223\212\202\260" /* ファイア投げ */},  // mParent
        1.00000000000f,    // 0x4
        0,                 // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\227\274\216\350\223\212\202\260" /* 両手投げ */},    // mParent
        1.00000000000f,  // 0x4
        0,               // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\164\203\100\203\103\203\101\203\130\203\163\203\223" /* ファイアスピン */},  // mParent
        1.00000000000f,      // 0x4
        0,                   // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\164\203\100\203\103\203\101\203\130\203\163\203\223\213\363\222\206" /* ファイアスピン空中 */},  // mParent
        1.00000000000f,          // 0x4
        0,                       // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\212\122\202\302\202\251\202\334\202\350\212\112\216\156" /* 崖つかまり開始 */},  // mParent
        1.00000000000f,      // 0x4
        0x5,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\212\122\202\302\202\251\202\334\202\350\222\206" /* 崖つかまり中 */},  // mParent
        1.00000000000f,    // 0x4
        0x5,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\212\122\202\302\202\251\202\334\202\350\217\111\227\271" /* 崖つかまり終了 */},  // mParent
        1.00000000000f,      // 0x4
        0x4,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\212\122\202\302\202\251\202\334\202\350\217\111\227\271\215\342" /* 崖つかまり終了坂 */},  // mParent
        1.00000000000f,        // 0x4
        0x4,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\212\122\202\302\202\251\202\334\202\350\215\176\202\350" /* 崖つかまり降り */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\212\122\202\323\202\361\202\316\202\350" /* 崖ふんばり */},  // mParent
        1.00000000000f,  // 0x4
        0x2,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\265\202\341\202\252\202\335\203\127\203\203\203\223\203\166" /* しゃがみジャンプ */},  // mParent
        1.00000000000f,        // 0x4
        0x6,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\265\202\341\202\252\202\335\203\127\203\203\203\223\203\166\222\205\222\156" /* しゃがみジャンプ着地 */},  // mParent
        1.00000000000f,            // 0x4
        0x6,                       // 0x8
        0.00000000000f,            // 0xC
        0.00000000000f,            // 0x10
        0.00000000000f,            // 0x14
        0,                         // 0x18
        0,                         // 0x1C
        0,                         // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\265\202\341\202\252\202\335\225\340\202\253" /* しゃがみ歩き */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\252\202\361\202\316\202\350\221\226\202\350" /* がんばり走り */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\225\240\202\316\202\242\203\127\203\203\203\223\203\166" /* 腹ばいジャンプ */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\113\212\212\202\350\203\127\203\203\203\223\203\166" /* 尻滑りジャンプ */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\221\117\225\373\217\254\203\137\203\201\201\133\203\127" /* 前方小ダメージ */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\214\343\225\373\217\254\203\137\203\201\201\133\203\127" /* 後方小ダメージ */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\213\255\220\247\203\127\203\203\203\223\203\166" /* 強制ジャンプ */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\167\203\212\203\122\203\166\203\136\201\133\203\127\203\203\203\223\203\166" /* ヘリコプタージャンプ */},  // mParent
        1.00000000000f,            // 0x4
        0x6,                       // 0x8
        0.00000000000f,            // 0xC
        0.00000000000f,            // 0x10
        0.00000000000f,            // 0x14
        0,                         // 0x18
        0,                         // 0x1C
        0,                         // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\222\206\203\137\203\201\201\133\203\127" /* 中ダメージ */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\222\206\203\137\203\201\201\133\203\127\213\363\222\206" /* 中ダメージ空中 */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\222\206\203\137\203\201\201\133\203\127\222\205\222\156" /* 中ダメージ着地 */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\222\206\214\343\203\137\203\201\201\133\203\127" /* 中後ダメージ */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\222\206\214\343\203\137\203\201\201\133\203\127\213\363\222\206" /* 中後ダメージ空中 */},  // mParent
        1.00000000000f,        // 0x4
        0x6,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\222\206\214\343\203\137\203\201\201\133\203\127\222\205\222\156" /* 中後ダメージ着地 */},  // mParent
        1.00000000000f,        // 0x4
        0x6,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\155\201\133\203\137\203\201\201\133\203\127" /* ノーダメージ */},  // mParent
        1.00000000000f,    // 0x4
        0,                 // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\211\212\203\137\203\201\201\133\203\127" /* 炎ダメージ */},  // mParent
        1.00000000000f,  // 0x4
        0,               // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\211\212\202\314\203\211\203\223\203\151\201\133" /* 炎のランナー */},  // mParent
        8.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\164\203\100\203\103\203\101\203\211\203\223\221\117\222\233" /* ファイアラン前兆 */},  // mParent
        1.00000000000f,        // 0x4
        0,                     // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\223\144\213\103\203\137\203\201\201\133\203\127" /* 電気ダメージ */},  // mParent
        1.00000000000f,    // 0x4
        0x1,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\223\144\213\103\203\137\203\201\201\133\203\127\217\111\227\271" /* 電気ダメージ終了 */},  // mParent
        1.00000000000f,        // 0x4
        0x6,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\164\203\100\203\103\203\101\203\137\203\223\203\130" /* ファイアダンス */},  // mParent
        6.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\214\343\223\135\202\323\202\301\202\306\202\321" /* 後転ふっとび */},  // mParent
        1.00000000000f,    // 0x4
        0x2,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\214\343\223\135\202\323\202\301\202\306\202\321\213\363\222\206" /* 後転ふっとび空中 */},  // mParent
        1.00000000000f,        // 0x4
        0x6,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\214\343\223\135\202\323\202\301\202\306\202\321\222\205\222\156" /* 後転ふっとび着地 */},  // mParent
        1.00000000000f,        // 0x4
        0x2,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\215\300\202\350\203\137\203\105\203\223" /* 座りダウン */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\212\264\223\144\203\137\203\105\203\223" /* 感電ダウン */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\211\212\203\137\203\105\203\223" /* 炎ダウン */},    // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\213\302\214\374\202\257\203\137\203\105\203\223" /* 仰向けダウン */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\230\353\202\271\203\137\203\105\203\223" /* 俯せダウン */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\223\336\227\216\203\137\203\105\203\223" /* 奈落ダウン */},  // mParent
        1.00000000000f,  // 0x4
        0x14,            // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\165\203\211\203\142\203\116\203\172\201\133\203\213\227\216\211\272" /* ブラックホール落下 */},  // mParent
        1.00000000000f,          // 0x4
        0x6,                     // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\214\201\133\203\130\225\211\202\257" /* レース負け */},  // mParent
        1.00000000000f,  // 0x4
        0xf,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\220\205\222\206\203\214\201\133\203\130\225\211\202\257" /* 水中レース負け */},  // mParent
        1.00000000000f,      // 0x4
        0xf,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\226\204\202\334\202\350\203\137\203\105\203\223" /* 埋まりダウン */},  // mParent
        1.00000000000f,    // 0x4
        0x1e,              // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\302\202\324\202\352\203\137\203\105\203\223" /* つぶれダウン */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\315\202\313\202\306\202\316\202\263\202\352" /* はねとばされ */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\202\315\202\313\202\306\202\316\202\263\202\352\217\111\227\271" /* はねとばされ終了 */},  // mParent
        1.00000000000f,        // 0x4
        0x6,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\145\203\152\203\130\203\126\203\207\203\142\203\147\215\266" /* テニスショット左 */},  // mParent
        1.25000000000f,        // 0x4
        0,                     // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\145\203\152\203\130\203\126\203\207\203\142\203\147\211\105" /* テニスショット右 */},  // mParent
        1.25000000000f,        // 0x4
        0,                     // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\145\203\152\203\130\203\126\203\207\203\142\203\147\222\206" /* テニスショット中 */},  // mParent
        1.00000000000f,        // 0x4
        0,                     // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\145\203\152\203\130\203\126\203\207\203\142\203\147\213\363" /* テニスショット空 */},  // mParent
        1.25000000000f,        // 0x4
        0,                     // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\145\203\152\203\130\203\105\203\107\203\103\203\147" /* テニスウエイト */},  // mParent
        1.00000000000f,      // 0x4
        0x6,                 // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\107\203\214\203\201\203\223\203\147\203\121\203\142\203\147" /* エレメントゲット */},  // mParent
        1.00000000000f,        // 0x4
        0x6,                   // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\107\203\214\203\201\203\223\203\147\203\121\203\142\203\147\220\332\222\156\222\206" /* エレメントゲット接地中 */},  // mParent
        1.00000000000f,              // 0x4
        0x6,                         // 0x8
        0.00000000000f,              // 0xC
        0.00000000000f,              // 0x10
        0.00000000000f,              // 0x14
        0,                           // 0x18
        0,                           // 0x1C
        0,                           // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\213\363\222\206\202\320\202\313\202\350" /* 空中ひねり */},  // mParent
        1.00000000000f,  // 0x4
        0,               // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\222\156\217\343\202\320\202\313\202\350" /* 地上ひねり */},  // mParent
        1.00000000000f,  // 0x4
        0,               // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\156\203\140\203\130\203\163\203\223" /* ハチスピン */},  // mParent
        1.00000000000f,  // 0x4
        0,               // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\156\203\140\203\130\203\163\203\223\213\363\222\206" /* ハチスピン空中 */},  // mParent
        1.00000000000f,      // 0x4
        0,                   // 0x8
        0.00000000000f,      // 0xC
        0.00000000000f,      // 0x10
        0.00000000000f,      // 0x14
        0,                   // 0x18
        0,                   // 0x1C
        0,                   // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\101\203\103\203\130\202\320\202\313\202\350" /* アイスひねり */},  // mParent
        1.00000000000f,    // 0x4
        0,                 // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\101\203\103\203\130\202\320\202\313\202\350\210\332\223\256" /* アイスひねり移動 */},  // mParent
        1.00000000000f,        // 0x4
        0,                     // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\101\203\103\203\130\202\320\202\313\202\350\220\303\216\176" /* アイスひねり静止 */},  // mParent
        1.00000000000f,        // 0x4
        0,                     // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\101\203\103\203\130\202\320\202\313\202\350\213\363\222\206" /* アイスひねり空中 */},  // mParent
        1.00000000000f,        // 0x4
        0,                     // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\120\201\133\203\147\203\101\203\116\203\132\203\213\203\127\203\203\203\223\203\166" /* スケートアクセルジャンプ */},  // mParent
        1.00000000000f,                // 0x4
        0,                             // 0x8
        0.00000000000f,                // 0xC
        0.00000000000f,                // 0x10
        0.00000000000f,                // 0x14
        0,                             // 0x18
        0,                             // 0x1C
        0,                             // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\120\201\133\203\147\203\127\203\203\203\223\203\1662" /* スケートジャンプ2 */},  // mParent
        1.00000000000f,         // 0x4
        0,                      // 0x8
        0.00000000000f,         // 0xC
        0.00000000000f,         // 0x10
        0.00000000000f,         // 0x14
        0,                      // 0x18
        0,                      // 0x1C
        0,                      // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\120\201\133\203\147\203\127\203\203\203\223\203\1663" /* スケートジャンプ3 */},  // mParent
        1.00000000000f,         // 0x4
        0,                      // 0x8
        0.00000000000f,         // 0xC
        0.00000000000f,         // 0x10
        0.00000000000f,         // 0x14
        0,                      // 0x18
        0,                      // 0x1C
        0,                      // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\120\201\133\203\147\222\205\222\156" /* スケート着地 */},  // mParent
        1.00000000000f,    // 0x4
        0,                 // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\120\201\133\203\147\220\303\216\176\222\205\222\156" /* スケート静止着地 */},  // mParent
        1.00000000000f,        // 0x4
        0,                     // 0x8
        0.00000000000f,        // 0xC
        0.00000000000f,        // 0x10
        0.00000000000f,        // 0x14
        0,                     // 0x18
        0,                     // 0x1C
        0,                     // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\214\251\202\351" /* 見る */},        // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\214\201\133\203\130\214\251\202\351" /* レース見る */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\112\203\212\203\112\203\212\214\300\212\105" /* カリカリ限界 */},  // mParent
        1.00000000000f,    // 0x4
        0x6,               // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\145\201\133\203\127\203\103\203\223A" /* ステージインA */},  // mParent
        1.00000000000f,     // 0x4
        0,                  // 0x8
        0.00000000000f,     // 0xC
        0.00000000000f,     // 0x10
        0.00000000000f,     // 0x14
        0,                  // 0x18
        0,                  // 0x1C
        0,                  // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\145\201\133\203\127\203\103\203\223B" /* ステージインB */},  // mParent
        1.00000000000f,     // 0x4
        0,                  // 0x8
        0.00000000000f,     // 0xC
        0.00000000000f,     // 0x10
        0.00000000000f,     // 0x14
        0,                  // 0x18
        0,                  // 0x1C
        0,                  // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\105\203\110\201\133\203\116\203\103\203\223" /* ウォークイン */},  // mParent
        1.00000000000f,    // 0x4
        0,                 // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2021]" /* スピンゲット[デモ1] */},  // mParent
        1.00000000000f,           // 0x4
        0,                        // 0x8
        0.00000000000f,           // 0xC
        0.00000000000f,           // 0x10
        0.00000000000f,           // 0x14
        0,                        // 0x18
        0,                        // 0x1C
        0,                        // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\163\203\223\203\121\203\142\203\147[\211\357\230\1421]" /* スピンゲット[会話1] */},  // mParent
        1.00000000000f,           // 0x4
        0,                        // 0x8
        0.00000000000f,           // 0xC
        0.00000000000f,           // 0x10
        0.00000000000f,           // 0x14
        0,                        // 0x18
        0,                        // 0x1C
        0,                        // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2022]" /* スピンゲット[デモ2] */},  // mParent
        1.00000000000f,           // 0x4
        0,                        // 0x8
        0.00000000000f,           // 0xC
        0.00000000000f,           // 0x10
        0.00000000000f,           // 0x14
        0,                        // 0x18
        0,                        // 0x1C
        0,                        // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\163\203\223\203\121\203\142\203\147[\211\357\230\1422]" /* スピンゲット[会話2] */},  // mParent
        1.00000000000f,           // 0x4
        0,                        // 0x8
        0.00000000000f,           // 0xC
        0.00000000000f,           // 0x10
        0.00000000000f,           // 0x14
        0,                        // 0x18
        0,                        // 0x1C
        0,                        // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2023]" /* スピンゲット[デモ3] */},  // mParent
        1.00000000000f,           // 0x4
        0,                        // 0x8
        0.00000000000f,           // 0xC
        0.00000000000f,           // 0x10
        0.00000000000f,           // 0x14
        0,                        // 0x18
        0,                        // 0x1C
        0,                        // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\163\203\223\203\121\203\142\203\147[\211\357\230\1423]" /* スピンゲット[会話3] */},  // mParent
        1.00000000000f,           // 0x4
        0,                        // 0x8
        0.00000000000f,           // 0xC
        0.00000000000f,           // 0x10
        0.00000000000f,           // 0x14
        0,                        // 0x18
        0,                        // 0x1C
        0,                        // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2024]" /* スピンゲット[デモ4] */},  // mParent
        1.00000000000f,           // 0x4
        0,                        // 0x8
        0.00000000000f,           // 0xC
        0.00000000000f,           // 0x10
        0.00000000000f,           // 0x14
        0,                        // 0x18
        0,                        // 0x1C
        0,                        // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2025]" /* スピンゲット[デモ5] */},  // mParent
        1.00000000000f,           // 0x4
        0,                        // 0x8
        0.00000000000f,           // 0xC
        0.00000000000f,           // 0x10
        0.00000000000f,           // 0x14
        0,                        // 0x18
        0,                        // 0x1C
        0,                        // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\163\203\223\203\121\203\142\203\147[\211\357\230\1424]" /* スピンゲット[会話4] */},  // mParent
        1.00000000000f,           // 0x4
        0,                        // 0x8
        0.00000000000f,           // 0xC
        0.00000000000f,           // 0x10
        0.00000000000f,           // 0x14
        0,                        // 0x18
        0,                        // 0x1C
        0,                        // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2026]" /* スピンゲット[デモ6] */},  // mParent
        1.00000000000f,           // 0x4
        0,                        // 0x8
        0.00000000000f,           // 0xC
        0.00000000000f,           // 0x10
        0.00000000000f,           // 0x14
        0,                        // 0x18
        0,                        // 0x1C
        0,                        // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2027]" /* スピンゲット[デモ7] */},  // mParent
        1.00000000000f,           // 0x4
        0,                        // 0x8
        0.00000000000f,           // 0xC
        0.00000000000f,           // 0x10
        0.00000000000f,           // 0x14
        0,                        // 0x18
        0,                        // 0x1C
        0,                        // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2028]" /* スピンゲット[デモ8] */},  // mParent
        1.00000000000f,           // 0x4
        0,                        // 0x8
        0.00000000000f,           // 0xC
        0.00000000000f,           // 0x10
        0.00000000000f,           // 0x14
        0,                        // 0x18
        0,                        // 0x1C
        0,                        // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\214\201\133\203\130\212\112\216\156" /* レース開始 */},  // mParent
        1.00000000000f,  // 0x4
        0x6,             // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\214\201\133\203\130\203\116\203\211\203\105\203\140\203\223\203\117\212\112\216\156" /* レースクラウチング開始 */},  // mParent
        1.00000000000f,              // 0x4
        0x6,                         // 0x8
        0.00000000000f,              // 0xC
        0.00000000000f,              // 0x10
        0.00000000000f,              // 0x14
        0,                           // 0x18
        0,                           // 0x1C
        0,                           // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\123\201\133\203\130\203\147\203\214\201\133\203\130\212\112\216\156" /* ゴーストレース開始 */},  // mParent
        1.00000000000f,          // 0x4
        0x6,                     // 0x8
        0.00000000000f,          // 0xC
        0.00000000000f,          // 0x10
        0.00000000000f,          // 0x14
        0,                       // 0x18
        0,                       // 0x1C
        0,                       // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\123\201\133\203\130\203\147\217\237\227\230" /* ゴースト勝利 */},  // mParent
        1.00000000000f,    // 0x4
        0,                 // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {"\203\123\201\133\203\130\203\147\217\157\214\273" /* ゴースト出現 */},  // mParent
        1.00000000000f,    // 0x4
        0,                 // 0x8
        0.00000000000f,    // 0xC
        0.00000000000f,    // 0x10
        0.00000000000f,    // 0x14
        0,                 // 0x18
        0,                 // 0x1C
        0,                 // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
    {
        {""},            // mParent
        0.00000000000f,  // 0x4
        0,               // 0x8
        0.00000000000f,  // 0xC
        0.00000000000f,  // 0x10
        0.00000000000f,  // 0x14
        0,               // 0x18
        0,               // 0x1C
        0,               // 0x1D
        {
            0,
            0,
            0,
            0,
        },  // 0x20
        {
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
            0.00000000000f,
        },        // 0x30
        0,        // 0x40
        0,        // 0x44
        nullptr,  // 0x48
    },
};

XanimeAuxInfo marioAnimeAuxTable[] = {{""}};

XanimeOfsInfo marioAnimeOfsTable[] = {
    {
        {"\203\137\203\201\201\133\203\127" /* ダメージ */},    // mParent
        0.00000000000f,  // 0x4
        10.0000000000f,  // 0x8
        10.0000000000f,  // 0xC
        0x1,             // 0x10
    },
    {
        {"\215\342\202\267\202\327\202\350\211\272\214\374\202\253\202\240\202\250\202\336\202\257" /* 坂すべり下向きあおむけ */},  // mParent
        0.00000000000f,              // 0x4
        2.00000000000f,              // 0x8
        2.00000000000f,              // 0xC
        0x1,                         // 0x10
    },
    {
        {"\215\342\202\267\202\327\202\350\217\343\214\374\202\253\202\244\202\302\202\324\202\271" /* 坂すべり上向きうつぶせ */},  // mParent
        0.00000000000f,              // 0x4
        2.00000000000f,              // 0x8
        2.00000000000f,              // 0xC
        0x1,                         // 0x10
    },
    {
        {"\203\136\201\133\203\223\203\165\203\214\201\133\203\114" /* ターンブレーキ */},  // mParent
        0.00000000000f,      // 0x4
        20.0000000000f,      // 0x8
        20.0000000000f,      // 0xC
        0x1,                 // 0x10
    },
    {
        {"\203\127\203\203\203\223\203\166" /* ジャンプ */},    // mParent
        0.00000000000f,  // 0x4
        24.0000000000f,  // 0x8
        24.0000000000f,  // 0xC
        0x1,             // 0x10
    },
    {
        {"\225\307\203\161\203\142\203\147" /* 壁ヒット */},    // mParent
        0.00000000000f,  // 0x4
        25.0000000000f,  // 0x8
        25.0000000000f,  // 0xC
        0x1,             // 0x10
    },
    {
        {"\225\307\203\161\203\142\203\147\222\205\222\156" /* 壁ヒット着地 */},  // mParent
        0.00000000000f,    // 0x4
        67.0000000000f,    // 0x8
        0.00000000000f,    // 0xC
        0,                 // 0x10
    },
    {
        {"\203\112\203\165\224\262\202\253" /* カブ抜き */},    // mParent
        0.00000000000f,  // 0x4
        49.0000000000f,  // 0x8
        49.0000000000f,  // 0xC
        0x1,             // 0x10
    },
    {
        {"\227\216\211\272" /* 落下 */},        // mParent
        0.00000000000f,  // 0x4
        29.0000000000f,  // 0x8
        29.0000000000f,  // 0xC
        0x1,             // 0x10
    },
    {
        {"\203\161\203\142\203\166\203\150\203\215\203\142\203\166\212\112\216\156" /* ヒップドロップ開始 */},  // mParent
        0.00000000000f,          // 0x4
        13.0000000000f,          // 0x8
        13.0000000000f,          // 0xC
        0x1,                     // 0x10
    },
    {
        {"\203\161\203\142\203\166\203\150\203\215\203\142\203\166" /* ヒップドロップ */},  // mParent
        0.00000000000f,      // 0x4
        9.00000000000f,      // 0x8
        9.00000000000f,      // 0xC
        0x1,                 // 0x10
    },
    {
        {"\203\130\203\163\203\223\203\161\203\142\203\166\203\150\203\215\203\142\203\166" /* スピンヒップドロップ */},  // mParent
        0.00000000000f,            // 0x4
        19.0000000000f,            // 0x8
        19.0000000000f,            // 0xC
        0x1,                       // 0x10
    },
    {
        {"\203\156\203\140\203\161\203\142\203\166\203\150\203\215\203\142\203\166\225\307\222\205\222\156" /* ハチヒップドロップ壁着地 */},  // mParent
        0.00000000000f,                // 0x4
        20.0000000000f,                // 0x8
        20.0000000000f,                // 0xC
        0,                             // 0x10
    },
    {
        {"\203\172\203\142\203\160\201\133\202\323\202\335\203\127\203\203\203\223\203\166A" /* ホッパーふみジャンプA */},  // mParent
        10.0000000000f,             // 0x4
        50.0000000000f,             // 0x8
        0.00000000000f,             // 0xC
        0x1,                        // 0x10
    },
    {
        {"\203\172\203\142\203\160\201\133\202\323\202\335\203\127\203\203\203\223\203\166B" /* ホッパーふみジャンプB */},  // mParent
        10.0000000000f,             // 0x4
        50.0000000000f,             // 0x8
        0.00000000000f,             // 0xC
        0x1,                        // 0x10
    },
    {
        {"\203\145\203\152\203\130\203\126\203\207\203\142\203\147\215\266" /* テニスショット左 */},  // mParent
        4.00000000000f,        // 0x4
        26.0000000000f,        // 0x8
        26.0000000000f,        // 0xC
        0,                     // 0x10
    },
    {
        {"\203\145\203\152\203\130\203\126\203\207\203\142\203\147\211\105" /* テニスショット右 */},  // mParent
        4.00000000000f,        // 0x4
        26.0000000000f,        // 0x8
        26.0000000000f,        // 0xC
        0,                     // 0x10
    },
    {
        {"\203\145\203\152\203\130\203\126\203\207\203\142\203\147\222\206" /* テニスショット中 */},  // mParent
        3.00000000000f,        // 0x4
        38.0000000000f,        // 0x8
        38.0000000000f,        // 0xC
        0,                     // 0x10
    },
    {
        {"\203\145\203\152\203\130\203\126\203\207\203\142\203\147\213\363" /* テニスショット空 */},  // mParent
        5.00000000000f,        // 0x4
        26.0000000000f,        // 0x8
        26.0000000000f,        // 0xC
        0,                     // 0x10
    },
    {
        {"\202\273\202\314\217\352\221\253\223\245\202\335" /* その場足踏み */},  // mParent
        0.00000000000f,    // 0x4
        17.0000000000f,    // 0x8
        8.00000000000f,    // 0xC
        0,                 // 0x10
    },
    {
        {"\203\165\203\214\201\133\203\114" /* ブレーキ */},    // mParent
        0.00000000000f,  // 0x4
        2.00000000000f,  // 0x8
        0.00000000000f,  // 0xC
        0x1,             // 0x10
    },
    {
        {"\203\126\203\207\201\133\203\147\222\205\222\156" /* ショート着地 */},  // mParent
        0.00000000000f,    // 0x4
        5.00000000000f,    // 0x8
        0.00000000000f,    // 0xC
        0,                 // 0x10
    },
    {
        {"\203\101\203\142\203\160\201\133\203\160\203\223\203\140" /* アッパーパンチ */},  // mParent
        22.0000000000f,      // 0x4
        23.0000000000f,      // 0x8
        0.00000000000f,      // 0xC
        0x1,                 // 0x10
    },
    {
        {"\202\265\202\341\202\252\202\335\203\101\203\142\203\160\201\133" /* しゃがみアッパー */},  // mParent
        0.00000000000f,        // 0x4
        6.00000000000f,        // 0x8
        0.00000000000f,        // 0xC
        0,                     // 0x10
    },
    {
        {"\203\130\203\120\203\114\203\210" /* スケキヨ */},    // mParent
        0.00000000000f,  // 0x4
        59.0000000000f,  // 0x8
        30.0000000000f,  // 0xC
        0x2,             // 0x10
    },
    {
        {"\203\130\203\120\203\114\203\210\222\105\217\157" /* スケキヨ脱出 */},  // mParent
        60.0000000000f,    // 0x4
        120.000000000f,    // 0x8
        0.00000000000f,    // 0xC
        0,                 // 0x10
    },
    {
        {"\223\301\216\352\203\105\203\107\203\103\203\1471A" /* 特殊ウエイト1A */},  // mParent
        0.00000000000f,      // 0x4
        494.000000000f,      // 0x8
        375.000000000f,      // 0xC
        0x2,                 // 0x10
    },
    {
        {"\223\301\216\352\203\105\203\107\203\103\203\1471B" /* 特殊ウエイト1B */},  // mParent
        0.00000000000f,      // 0x4
        254.000000000f,      // 0x8
        135.000000000f,      // 0xC
        0x2,                 // 0x10
    },
    {
        {"\203\101\203\103\203\130\202\320\202\313\202\350" /* アイスひねり */},  // mParent
        0.00000000000f,    // 0x4
        35.0000000000f,    // 0x8
        35.0000000000f,    // 0xC
        0,                 // 0x10
    },
    {
        {"\203\164\203\100\203\103\203\101\203\130\203\163\203\223\213\363\222\206" /* ファイアスピン空中 */},  // mParent
        0.00000000000f,          // 0x4
        30.0000000000f,          // 0x8
        30.0000000000f,          // 0xC
        0,                       // 0x10
    },
    {
        {"\212\122\202\302\202\251\202\334\202\350\217\111\227\271\215\342" /* 崖つかまり終了坂 */},  // mParent
        0.00000000000f,        // 0x4
        27.0000000000f,        // 0x8
        27.0000000000f,        // 0xC
        0,                     // 0x10
    },
    {
        {"\221\117\225\307\203\105\203\107\203\103\203\147" /* 前壁ウエイト */},  // mParent
        57.0000000000f,    // 0x4
        59.0000000000f,    // 0x8
        57.0000000000f,    // 0xC
        0x1,               // 0x10
    },
    {
        {"\203\164\201\133\203\164\203\100\203\103\203\136\201\133\220\303\216\176" /* フーファイター静止 */},  // mParent
        0.00000000000f,          // 0x4
        159.000000000f,          // 0x8
        40.0000000000f,          // 0xC
        0x2,                     // 0x10
    },
    {
        {"\202\320\202\353\202\242\203\116\203\103\203\142\203\116" /* ひろいクイック */},  // mParent
        0.00000000000f,      // 0x4
        26.0000000000f,      // 0x8
        0.00000000000f,      // 0xC
        0x1,                 // 0x10
    },
    {
        {""},            // mParent
        0.00000000000f,  // 0x4
        0.00000000000f,  // 0x8
        0.00000000000f,  // 0xC
        0,               // 0x10
    },
};

XanimeSwapTable luigiAnimeSwapTable[] = {
    {"Run", "LuigiRun"},
    {"Jump", "LuigiJump"},
    {"JumpRoll", "LuigiJumpRoll"},
    {"JumpBack", "LuigiJumpBack"},
    {"RunEnd", "LuigiRunEnd"},
    {"Spin", "LuigiSpin"},
    {"SpinGround", "LuigiSpinGround"},
    {"SpaceFlyShort", "LuigiSpaceFlyShort"},
    {"Wait", "LuigiWait"},
    {"WaitSlopeL", "LuigiWaitSlopeL"},
    {"WaitSlopeR", "LuigiWaitSlopeR"},
    {"WaitSlopeU", "LuigiWaitSlopeU"},
    {"WaitSlopeD", "LuigiWaitSlopeD"},
    {"", nullptr},
};
