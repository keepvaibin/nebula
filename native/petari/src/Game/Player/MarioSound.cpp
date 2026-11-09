#include "Game/AudioLib/AudAnmSoundObject.hpp"
#include "Game/AudioLib/AudSoundId.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioWait.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Sound/Sound.hpp"
#include "Game/Util/StringUtil.hpp"

struct SoundList {
    const char* name;
    u32 mSoundId;
    u32 mFlags;
    const char* mWaterName;  // also played in water when kind()'s upper bits are 0x4 or 0x8

    // The flags' top byte: the low two bits pick the call (0 actor, 1 actor level, 2 system, 3 system level).
    u8 kind() const {
        return static_cast< u8 >(mFlags >> 24);
    }
};

const SoundList soundlist[] = {
    {"声小ジャンプ", SE_PV_JUMP_S, 0, nullptr},
    {"声中ジャンプ", SE_PV_JUMP_M, 0, nullptr},
    {"声大ジャンプ", SE_PV_JUMP_L, 0, nullptr},
    {"声後ジャンプ", SE_PV_JUMP_TURN, 0, nullptr},
    {"声幅ジャンプ", SE_PV_JUMP_LONG, 0, nullptr},
    {"声物ジャンプ", SE_PV_JUMP_JOY, 0, nullptr},
    {"声高飛び込み", SE_PV_HIGH_DIVE, 0, nullptr},
    {"声尻ドロップ", SE_PV_HIP_DROP, 0, nullptr},
    {"声尻ドロップ着地", SE_PV_HIP_DROP_LAND, 0, nullptr},
    {"声着地停止", SE_PV_LAND, 0, nullptr},
    {"声ステージイン着地", SE_PV_LAND_COOL, 0, nullptr},
    {"声小ダメージ", SE_PV_DAMAGE_S, 0, nullptr},
    {"声大ダメージ", SE_PV_DAMAGE_L, 0, nullptr},
    {"声最終ダメージ", SE_PV_LAST_DAMAGE, 0, nullptr},
    {"声炎ダメージ", SE_PV_BURN, 0, nullptr},
    {"声電気ダメージ", SE_PV_ELEC_DAMAGE, 0, nullptr},
    {"声電気ダメージ終了", SE_PV_ELEC_DAMAGE_RECOVER, 0, nullptr},
    {"声倒れ", SE_PV_DOWN, 0, nullptr},
    {"声落下死亡", SE_PV_FALL_DIE, 0, nullptr},
    {"声しびれ", SE_PV_DAMAGE_S, 0, nullptr},
    {"声氷ダメージ", SE_PV_FREEZE, 0, nullptr},
    {"声つぶれダメージ", SE_PV_DIE_ROCK_CRASH, 0, nullptr},
    {"声スピン", SE_PV_SPIN, 0, nullptr},
    {"声スピンキャンセル", SE_PV_SPIN_CANCEL, 0, nullptr},
    {"声パンチ", SE_PV_PUNCH, 0, nullptr},
    {"声踏み", SE_PV_STOMP, 0, nullptr},
    {"声蹴り", SE_PV_KICK, 0, nullptr},
    {"声トルネード", SE_PV_TWIST_START, 0, nullptr},
    {"声壁反射", SE_PV_GUARD, 0, nullptr},
    {"声投げ", SE_PV_THROW, 0, nullptr},
    {"声壁押し", SE_PV_LIFT_UP, 0, nullptr},
    {"声壁体当たり", SE_PV_WALL_HIT_BODY, 0, nullptr},
    {"声スピン尻ドロップ", SE_PV_PUNCH, 0, nullptr},
    {"声軽い力み", SE_PV_CATCH, 0, nullptr},
    {"声崖つかまり", SE_PV_CLIFF_FALL_HANG, 0, nullptr},
    {"声崖つかまり終了", SE_PV_CLIFF_CLIMB, 0, nullptr},
    {"声炎ダメージ中", SE_PV_BURN_RUN, 0, nullptr},
    {"声炎ダメージ終了", SE_PV_BURN_RECOVER, 0, nullptr},
    {"声氷ダメージ終了", SE_PV_FREEZE_RECOVER, 0, nullptr},
    {"声針ダメージ", SE_PV_NEEDLE_DAMAGE, 0, nullptr},
    {"声針ダメージ中", SE_PV_NEEDLE_DAMAGE_RUN, 0, nullptr},
    {"声針ダメージ終了", SE_PV_NEEDLE_DAMAGE_RECOVER, 0, nullptr},
    {"声水中ダメージ", SE_PV_DAMAGE_S_WATER, 0, nullptr},
    {"声水中最終ダメージ", SE_PV_LAST_DAMAGE_WATER, 0, nullptr},
    {"声冷水ダメージ", SE_PV_COLD_WATER_DAMAGE, 0, nullptr},
    {"声スケキヨ開始", SE_PV_BURY_HEAD, 0, nullptr},
    {"声スケキヨ終了", SE_PV_BURY_HEAD_RECOVER, 0, nullptr},
    {"声足埋まり開始", SE_PV_BURY_FOOT, 0, nullptr},
    {"声足埋まり終了", SE_PV_BURY_FOOT_RECOVER, 0, nullptr},
    {"声あくび", SE_PV_YAWN, 0, nullptr},
    {"声いびき１", SE_PV_SLEEP_1, 0, nullptr},
    {"声いびき２", SE_PV_SLEEP_2, 0, nullptr},
    {"声噴水ジャンプ", SE_PV_UPSET, 0, nullptr},
    {"声ランニングキック", SE_PV_KICK, 0, nullptr},
    {"声砂脱出", SE_PV_LIFT_UP, 0, nullptr},
    {"声投げられ", SE_PV_THROWN, 0, nullptr},
    {"声慌て", SE_PV_UPSET, 0, nullptr},
    {"声しゃがむ", SE_PV_SQUAT, 0, nullptr},
    {"声砂沈み", SE_PV_DIE_MUD, 0, nullptr},
    {"声砂沈み死亡", SE_PV_MUD_SINK, 0, nullptr},
    {"声沼沈み", SE_PV_DIE_MUD, 0, nullptr},
    {"声沼沈み死亡", SE_PV_MUD_SINK, 0, nullptr},
    {"足音左", SE_PM_FOOTNOTE_L, 0, nullptr},
    {"足音右", SE_PM_FOOTNOTE_R, 0, nullptr},
    {"ジャンプ踏切", SE_PM_JUMP, 0x4000000, "水跳ねジャンプ"},
    {"着地", SE_PM_LAND, 0x4000000, "水跳ね着地"},
    {"重い着地", SE_PM_LAND_HEAVY, 0, nullptr},
    {"掴み", SE_PM_GRAB_OBJ, 0, nullptr},
    {"ぶらさがり", SE_PM_HAND, 0, nullptr},
    {"尻ドロップ回転", SE_PM_PRE_HIPDROP, 0, nullptr},
    {"尻ドロップ着地", SE_PM_HIPDROP, 0x4000000, "水跳ね着地"},
    {"スピン尻ドロップ回転", SE_PM_SPIN_HIP_DROP_TURN, 0, nullptr},
    {"スピン尻ドロップ落下", SE_PM_LV_SPIN_HIP_DROP_FALL, 0x1000000, nullptr},
    {"中ジャンプ", SE_PM_JUMP_M, 0, nullptr},
    {"大ジャンプ", SE_PM_JUMP_L, 0, nullptr},
    {"後ジャンプ", SE_PM_JUMP_TURN, 0, nullptr},
    {"幅ジャンプ", SE_PM_JUMP_LONG, 0, nullptr},
    {"トルネードジャンプ", SE_PM_TWIST_JUMP, 0, nullptr},
    {"スピンジャンプ", SE_PM_SPIN_ATTACK, 0x4000000, "水跳ねジャンプ"},
    {"トランポリンジャンプ小", SE_OJ_TRAMPOLINE_BOUND_S, 0, nullptr},
    {"トランポリンジャンプ大", SE_OJ_TRAMPOLINE_BOUND_L, 0, nullptr},
    {"スリップアップ", SE_PM_SLIP_UP, 0, nullptr},
    {"ダメージ", SE_PM_DAMAGE_S, 0, nullptr},
    {"炎ダメージ", SE_PM_BURN, 0, nullptr},
    {"針ダメージ", SE_PM_NEEDLE_DAMAGE, 0, nullptr},
    {"電気ダメージ", SE_PM_ELEC_DAMAGE, 0, nullptr},
    {"壁反射", SE_PM_SPIN_HIT_WALL, 0, nullptr},
    {"プレスダメージ", SE_PM_DAMAGE_STOMPED, 0, nullptr},
    {"氷ダメージ", SE_PM_ICE_DAMAGE, 0, nullptr},
    {"倒れ", SE_PM_FALLDOWN_S, 0x4000000, "水跳ね左足"},
    {"吹っ飛び倒れ", SE_PM_FALLDOWN_M, 0x4000000, "水跳ね着地"},
    {"坂滑り", SE_PM_LV_SLIP_SLIP_CODE, 0x9000000, "水面滑り"},
    {"ルイージ滑り", SE_PM_LV_LUIGI_WALK_SLIP, 0x9000000, "水面滑り"},
    {"パンチ風切り", SE_PM_PUNCH_SHOOT, 0, nullptr},
    {"炎ダメージ復帰バウンド", SE_PM_BURN_JUMP, 0, nullptr},
    {"炎ダメージ炎上中", SE_PM_LV_BURNING, 0x1000000, nullptr},
    {"氷ダメージ終了", SE_PM_ICE_DAMAGE_RECOVER, 0, nullptr},
    {"引き戻し基本", SE_PM_LV_PULL_BACK_BASE, 0x1000000, nullptr},
    {"引き戻し浮遊", SE_PM_LV_PULL_BACK_FLY, 0x1000000, nullptr},
    {"引き戻し泡破裂", SE_OJ_GCAPTURE_RELEASE, 0, nullptr},
    {"はねとばされ", SE_PM_LV_FLIP_DAMAGE_TURN, 0x1000000, nullptr},
    {"結界ヒット", SE_PM_KAMECK_BARRIER_BOUND, 0, nullptr},
    {"壁衝突", SE_PM_WALL_HIT_BODY, 0, nullptr},
    {"投げられ", SE_PM_FLIP_AWAY, 0, nullptr},
    {"ヘリコプタージャンプ", SE_PM_HELI_JUMP, 0, nullptr},
    {"滞空開始", SE_PM_HELI_JUMP_AIR_START, 0, nullptr},
    {"滞空中", SE_PM_LV_HELI_JUMP_AIR, 0x1000000, nullptr},
    {"ホッパー跳ね返り", SE_PM_HOPPER_BOUND, 0, nullptr},
    {"ホッパージャンプ溜め", SE_PM_LV_HOPPER_PRE_JUMP, 0x1000000, nullptr},
    {"ホッパージャンプ", SE_PM_HOPPER_JUMP, 0, nullptr},
    {"テレサ浮遊", SE_PM_LV_TERESA_MARIO_FLY, 0x1000000, nullptr},
    {"テレサ踏ん張り", SE_PM_LV_AIR_WALK, 0x1000000, nullptr},
    {"テレサ壁反射", SE_PM_TERESA_MARIO_BOUND, 0, nullptr},
    {"テレサ消える", SE_PM_TERESA_MARIO_VANISH, 0, nullptr},
    {"テレサ現れる", SE_PM_TERESA_MARIO_APPEAR, 0, nullptr},
    {"テレサ風に乗る", SE_PM_LV_T_MARIO_RIDE_WIND, 0x1000000, nullptr},
    {"ハチ飛行中", SE_PM_LV_BEE_MARIO_FLY, 0x1000000, nullptr},
    {"スケキヨ開始", SE_PM_BURY_START, 0, nullptr},
    {"スケキヨ終了", SE_PM_BURY_END, 0, nullptr},
    {"スケキヨ終了スピン", SE_PM_BURY_END_SPIN, 0, nullptr},
    {"スケート足", SE_PM_SKATE, 0, nullptr},
    {"スケート滑り", SE_PM_LV_SKATE_SLIP, 0x1000000, nullptr},
    {"スケートジャンプ", SE_PM_SKATE_JUMP, 0, nullptr},
    {"スケート着地", SE_PM_SKATE_LAND, 0, nullptr},
    {"スケートスピン", SE_PM_SKATE_SPIN, 0, nullptr},
    {"ハチ壁くっつき", SE_PM_BEE_WALL_LAND, 0, nullptr},
    {"ハチ壁歩き左", SE_PM_BEE_WALL_WALK_L, 0, nullptr},
    {"ハチ壁歩き右", SE_PM_BEE_WALL_WALK_R, 0, nullptr},
    {"ハチ壁ジャンプ", SE_PM_BEE_WALL_JUMP, 0, nullptr},
    {"砂沈み", SE_PM_LV_SAND_SINK, 0x1000000, nullptr},
    {"砂脱出", SE_PM_SAND_OUT, 0, nullptr},
    {"砂強制沈み", SE_PM_LV_SAND_SINK_FORCE, 0x1000000, nullptr},
    {"砂死亡", SE_PM_LV_SAND_DIE, 0x1000000, nullptr},
    {"沼強制沈み", SE_PM_LV_MUD_SINK_FORCE, 0x1000000, nullptr},
    {"沼死亡", SE_PM_LV_MUD_DIE, 0x1000000, nullptr},
    {"毒沼脱出", SE_PM_POISON_MUD_OUT, 0, nullptr},
    {"毒沼ダメージ", SE_PM_POISON_MUD_DAMAGE, 0, nullptr},
    {"ダークマター沈み", SE_PM_LV_DARK_MATTER_IN, 0x1000000, nullptr},
    {"惑星貫通中", SE_PM_LV_WARP_STRAIGHT, 0x1000000, nullptr},
    {"惑星貫通終了", SE_PM_WARP_STRAIGHT_END, 0, nullptr},
    {"フー滞空中", SE_PM_LV_FOO_FLY_WAIT, 0x1000000, nullptr},
    {"フー加速", SE_PM_FOO_ACCEL, 0, nullptr},
    {"フー飛行中", SE_PM_LV_FOO_FLYING, 0x1000000, nullptr},
    {"フーブレーキ", SE_PM_FOO_BRAKE, 0, nullptr},
    {"ワープポッド入り", SE_PM_WARP_POD_IN, 0, nullptr},
    {"ワープポッド出", SE_PM_WARP_POD_OUT, 0, nullptr},
    {"ワープポッド移動", SE_PM_LV_WARP_POD_MOVE, 0x1000000, nullptr},
    {"スペシャルダッシュ強", SE_PM_RACE_START_DASH_L, 0, nullptr},
    {"スペシャルダッシュ弱", SE_PM_RACE_START_DASH_S, 0, nullptr},
    {"ダッシュ加速強成功", SE_SY_GET_DASH_RING, 0x2000000, nullptr},
    {"ブラックホール吸い込まれ", SE_PM_BLACK_HOLE_IN, 0, nullptr},
    {"水歩行突入", SE_PM_WALK_TO_SWIM, 0, nullptr},
    {"水歩行脱出", SE_PM_SWIM_TO_WALK, 0, nullptr},
    {"水面ウエイト", SE_PM_WAIT_ON_WATER, 0, nullptr},
    {"水中ウエイト", SE_PM_WAIT_UNDER_WATER, 0, nullptr},
    {"水落下突入", SE_PM_DIVE_TO_WATER, 0, nullptr},
    {"水ジャンプ脱出", SE_PM_JUMP_FROM_WATER, 0, nullptr},
    {"水面一掻き", SE_PM_SWIM_ACCEL_ON_WATER, 0, nullptr},
    {"水中一掻き", SE_PM_SWIM_ACCEL_IN_WATER, 0, nullptr},
    {"水面バタ足", SE_PM_LV_SWIM_ON_WATER, 0x1000000, nullptr},
    {"水中バタ足", SE_PM_LV_SWIM_IN_WATER, 0x1000000, nullptr},
    {"水面潜り", SE_PM_DIVE_WATER_ROLLING, 0, nullptr},
    {"水中潜り", SE_PM_DIVE_FAST_IN_WATER, 0, nullptr},
    {"水面スピン開始", SE_PM_TORNADE_ON_WATER_ST, 0, nullptr},
    {"水面スピン", SE_PM_LV_TORNADE_ON_WATER, 0x1000000, nullptr},
    {"水中スピン開始", SE_PM_TORNADE_IN_WATER_ST, 0, nullptr},
    {"水中スピン", SE_PM_LV_TORNADE_IN_WATER, 0x1000000, nullptr},
    {"水面ダメージ", SE_PM_DAMAGE_ON_WATER, 0, nullptr},
    {"水中ダメージ", SE_PM_DAMAGE_IN_WATER, 0, nullptr},
    {"亀ジェット泳ぎ", SE_OJ_LV_TURTLE_JET_SWIM, 0x1000000, nullptr},
    {"水底接触", SE_PM_LV_TURTLE_SEA_SMOKE, 0x1000000, nullptr},
    {"亀壁ヒット", SE_OJ_TURTLE_JET_BOUND_W, 0, nullptr},
    {"亀ブレーキ", SE_PM_TURTLE_BRAKE, 0, nullptr},
    {"亀加速", SE_PM_TURTLE_ACCEL, 0, nullptr},
    {"水跳ね左足", SE_PM_FOOTNOTE_L_W, 0, nullptr},
    {"水跳ね右足", SE_PM_FOOTNOTE_R_W, 0, nullptr},
    {"水跳ね左足小", SE_PM_FOOTNOTE_SUB_L_W, 0, nullptr},
    {"水跳ね右足小", SE_PM_FOOTNOTE_SUB_R_W, 0, nullptr},
    {"水跳ねジャンプ", SE_PM_JUMP_W, 0, nullptr},
    {"水跳ね着地", SE_PM_LAND_W, 0, nullptr},
    {"水跳ね手", SE_PM_HAND_W, 0, nullptr},
    {"水弾かれ", SE_PM_WATER_BOUND, 0, nullptr},
    {"水面滑り", SE_PM_LV_SLIP_WATER, 0x1000000, nullptr},
    {"トルネード風", SE_PM_LV_TWIST_WIND, 0x1000000, nullptr},
    {"スリップ", SE_PM_LV_SLIP, 0x9000000, "水面滑り"},
    {"最後の一撃", SE_PM_LAST_DAMAGE, 0, nullptr},
    {"空中ふんばり", SE_PM_LV_AIR_WALK, 0x1000000, nullptr},
    {"スピン許可", SE_PM_SPIN_ENABLE, 0, nullptr},
    {"スピン回復終了", SE_PM_SPIN_RECOVER_END, 0, nullptr},
    {"トルネード飛行", SE_PM_LV_TORNADE_FLYING, 0x1000000, nullptr},
    {"ライフ警告", SE_SY_LV_LIFE_ALERT, 0x3000000, nullptr},
    {"ライフ回復", SE_SY_LIFE_RECOVER, 0x2000000, nullptr},
    {"水中ライフ減少", SE_SY_WATER_LIFE_DEC, 0x2000000, nullptr},
    {"酸素減少警告", SE_SY_OXYGEN_ALERT, 0x2000000, nullptr},
    {"無酸素警告", SE_SY_OXYGEN_ZERO_ALERT, 0x2000000, nullptr},
    {"酸素回復", SE_SY_INC_OXYGEN_ONE, 0x2000000, nullptr},
    {"酸素完全回復", SE_SY_INC_OXYGEN_FULL, 0x2000000, nullptr},
    {"水面酸素回復", SE_SY_LV_RECOVER_OXYGEN, 0x3000000, nullptr},
    {"水面酸素完全回復", SE_SY_RECOVER_OXYGEN_FULL, 0x2000000, nullptr},
    {"無酸素ダメージ", SE_SY_NO_OXYGEN, 0x2000000, nullptr},
    {"ハチ体力回復", SE_SY_LV_INC_AIR_WALK_TIMER, 0x3000000, nullptr},
    {"ハチ体力完全回復", SE_SY_INC_AIR_WALK_TIMER_F, 0x2000000, nullptr},
    {"ハチ体力切れ", SE_SY_NO_AIR_WALK_TIMER, 0x2000000, nullptr},
    {"変身解除", SE_SY_MORPH_END, 0x2000000, nullptr},
    {"呪い解除", SE_SY_MORPH_TO_NORMAL, 0x2000000, nullptr},
    {"", 0, 0, nullptr},
};

u32 Mario::initSoundTable(const SoundList* pList) {
    u32 count = 0;
    while (pList[count].name[0] != '\0') {
        count++;
    }

    return count;
}

void Mario::initSound() {
    u32 count = initSoundTable(soundlist);
    mSounds.reset(count);
    for (u32 i = 0; i < count; i++) {
        mSounds.add(soundlist[i].name, i);
    }

    mRunningBas = nullptr;
    mRunningDamageWait = false;
}

// The reference also tries the hash sum of "声" and the name (a voice by its short name); no sum matches a listed
// name, so that second lookup never finds one.
void Mario::playSoundJ(const char* pSoundName, s32 timing) {
    const u32 index = mSounds.find(pSoundName);
    if (index != MR::NameIndex::cNoValue) {
        switch (soundlist[index].kind() & 0x3) {
        case 0:
            MR::startSound(mActor, soundlist[index].mSoundId, timing);
            break;

        case 2:
            MR::startSystemSE(soundlist[index].mSoundId, timing);
            break;

        case 1:
            MR::startLevelSound(mActor, soundlist[index].mSoundId, timing);
            break;

        case 3:
            MR::startSystemLevelSE(soundlist[index].mSoundId, timing);
            break;
        }

        switch (soundlist[index].kind() & ~0x3) {
        case 0x4:
        case 0x8:
            if (mDrawStates.inWater || mDrawStates.inShallowWater) {
                playSoundJ(soundlist[index].mWaterName, -1);
            }
            break;
        }
    }
}

void Mario::stopSoundJ(const char* pSoundName, u32 delay) {
    const u32 index = mSounds.find(pSoundName);
    if (index != MR::NameIndex::cNoValue) {
        switch (soundlist[index].kind() & 0x3) {
        case 0:
            MR::stopSound(mActor, soundlist[index].mSoundId, delay);
            break;

        case 1:
            break;

        case 2:
            MR::stopSystemSE(soundlist[index].mSoundId, delay);
            break;

        case 3:
            break;
        }
    }
}

void Mario::startBas(const char* pAnimName, bool isLoop, f32 startFrame, f32 speed) {
    if (mActor->mSoundObject != nullptr) {
        ResourceHolder* pHolder = MR::getResourceHolder(mActor);
        const SoundAnimation* pRes = nullptr;

        if (pAnimName != nullptr && pHolder->mBasResTable->isExistRes(pAnimName)) {
            pRes = static_cast< SoundAnimation* >(pHolder->mBasResTable->getRes(pAnimName));
        }

        if (pRes != nullptr) {
            mActor->mSoundObject->startAnimation(pRes, isLoop, startFrame, speed);
        } else {
            mActor->mSoundObject->removeAnimation();
        }

        mRunningBas = pAnimName;
        mRunningDamageWait = pAnimName != nullptr && MR::isEqualString(pAnimName, "DamageWait");
    }
}

bool Mario::isRunningBas(const char* pAnimName) const {
    if (mRunningBas == nullptr) {
        return false;
    }

    if (MR::isEqualString(mRunningBas, pAnimName)) {
        return true;
    }

    return false;
}

void Mario::skipBas(f32 frame) {
    mActor->mSoundObject->skip(frame);
}

void Mario::playSoundTeresaFlying() {
    if (getPlayerMode() != 6) {
        return;
    }

    s32 timing = 100;
    if (getCurrentStatus() == 0x1C) {
        if (mWait->mStep == 0) {
            timing = 100 - static_cast< s32 >(mWait->mTimer);
            if (timing < 0) {
                timing = 0;
            }
        } else {
            timing = 0;
        }
    }

    playSound("テレサ浮遊", timing);
}

void Mario::playSoundTrampleCombo(u8 combo) {
    if (combo >= 7) {
        return;
    }

    MR::startSystemSE("SE_SY_TRAMPLE_COMBO", combo);
}

void Mario::setSeVersion(u32 version) {
    MR::setSeVersion(mActor, version);
}
