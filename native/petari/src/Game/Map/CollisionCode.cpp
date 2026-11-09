#include "Game/Map/CollisionCode.hpp"
#include "Game/Map/HitInfo.hpp"

CollisionCode::CollisionCode() : mFloorTable(), mWallTable(), mSoundTable(), mCameraTable() {
    createFloorTable();
    createWallTable();
    createSoundTable();
    createCameraTable();
}

CodeTable::CodeTable(u32 numMax) : mIndex(numMax) {
}

void CodeTable::add(const char* pName, u32 code) {
    mIndex.add(pName, code);
}

void CollisionCode::createFloorTable() {
    mFloorTable = new CodeTable(CollisionFloorCode_End);
    mFloorTable->add("Normal", CollisionFloorCode_Normal);
    mFloorTable->add("Death", CollisionFloorCode_Death);
    mFloorTable->add("Slip", CollisionFloorCode_Slip);
    mFloorTable->add("NoSlip", CollisionFloorCode_NoSlip);
    mFloorTable->add("DamageNormal", CollisionFloorCode_DamageNormal);
    mFloorTable->add("Ice", CollisionFloorCode_Ice);
    mFloorTable->add("JumpLow", CollisionFloorCode_JumpLow);
    mFloorTable->add("JumpMiddle", CollisionFloorCode_JumpMiddle);
    mFloorTable->add("JumpHigh", CollisionFloorCode_JumpHigh);
    mFloorTable->add("Slider", CollisionFloorCode_Slider);
    mFloorTable->add("DamageFire", CollisionFloorCode_DamageFire);
    mFloorTable->add("JumpNormal", CollisionFloorCode_JumpNormal);
    mFloorTable->add("FireDance", CollisionFloorCode_FireDance);
    mFloorTable->add("Sand", CollisionFloorCode_Sand);
    mFloorTable->add("Glass", CollisionFloorCode_Glass);
    mFloorTable->add("DamageElectric", CollisionFloorCode_DamageElectric);
    mFloorTable->add("PullBack", CollisionFloorCode_PullBack);
    mFloorTable->add("Sink", CollisionFloorCode_Sink);
    mFloorTable->add("SinkPoison", CollisionFloorCode_SinkPoison);
    mFloorTable->add("Slide", CollisionFloorCode_Slide);
    mFloorTable->add("WaterBottomH", CollisionFloorCode_WaterBottomH);
    mFloorTable->add("WaterBottomM", CollisionFloorCode_WaterBottomM);
    mFloorTable->add("WaterBottomL", CollisionFloorCode_WaterBottomL);
    mFloorTable->add("Wet", CollisionFloorCode_Wet);
    mFloorTable->add("Needle", CollisionFloorCode_Needle);
    mFloorTable->add("SinkDeath", CollisionFloorCode_SinkDeath);
    mFloorTable->add("Snow", CollisionFloorCode_Snow);
    mFloorTable->add("RailMove", CollisionFloorCode_RailMove);
    mFloorTable->add("AreaMove", CollisionFloorCode_AreaMove);
    mFloorTable->add("Press", CollisionFloorCode_Press);
    mFloorTable->add("NoStampSand", CollisionFloorCode_NoStampSand);
    mFloorTable->add("SinkDeathMud", CollisionFloorCode_SinkDeathMud);
    mFloorTable->add("Brake", CollisionFloorCode_Brake);
    mFloorTable->add("GlassIce", CollisionFloorCode_GlassIce);
    mFloorTable->add("JumpParasol", CollisionFloorCode_JumpParasol);
}

void CollisionCode::createWallTable() {
    mWallTable = new CodeTable(CollisionWallCode_End);
    mWallTable->add("Normal", CollisionWallCode_Normal);
    mWallTable->add("NotWallJump", CollisionWallCode_NotWallJump);
    mWallTable->add("NotWallSlip", CollisionWallCode_NotWallSlip);
    mWallTable->add("NotGrab", CollisionWallCode_NotGrab);
    mWallTable->add("GhostThroughCode", CollisionWallCode_GhostThroughCode);
    mWallTable->add("NotSideStep", CollisionWallCode_NotSideStep);
    mWallTable->add("Rebound", CollisionWallCode_Rebound);
    mWallTable->add("Fur", CollisionWallCode_Fur);
    mWallTable->add("NoAction", CollisionWallCode_NoAction);
}

void CollisionCode::createSoundTable() {
    mSoundTable = new CodeTable(CollisionSoundCode_End);
    mSoundTable->add("null", CollisionSoundCode_Null);
    mSoundTable->add("Soil", CollisionSoundCode_Soil);
    mSoundTable->add("Lawn", CollisionSoundCode_Lawn);
    mSoundTable->add("Stone", CollisionSoundCode_Stone);
    mSoundTable->add("Marble", CollisionSoundCode_Marble);
    mSoundTable->add("WoodThick", CollisionSoundCode_WoodThick);
    mSoundTable->add("WoodThin", CollisionSoundCode_WoodThin);
    mSoundTable->add("Metal", CollisionSoundCode_Metal);
    mSoundTable->add("Snow", CollisionSoundCode_Snow);
    mSoundTable->add("Ice", CollisionSoundCode_Ice);
    mSoundTable->add("Shallow", CollisionSoundCode_Shallow);
    mSoundTable->add("Sand", CollisionSoundCode_Sand);
    mSoundTable->add("Beach", CollisionSoundCode_Beach);
    mSoundTable->add("Carpet", CollisionSoundCode_Carpet);
    mSoundTable->add("Mud", CollisionSoundCode_Mud);
    mSoundTable->add("Honey", CollisionSoundCode_Honey);
}

void CollisionCode::createCameraTable() {
    mCameraTable = new CodeTable(CollisionCameraCode_End);
    mCameraTable->add("NoThrough", CollisionCameraCode_NoThrough);
    mCameraTable->add("Through", CollisionCameraCode_Through);
}

u32 CollisionCode::getCameraID(const Triangle& rTriangle) {
    return getCameraID(rTriangle.getAttributes());
}

u32 CollisionCode::getCode(const TableIter& rIter, CodeTable* pCodeTable, const char* pName) {
    if (!rIter.isValid()) {
        return 0;
    }

    if (rIter.mInfo->getColumnKind(pName) != TableKind_Text) {
        u32 code = 0;

        if (rIter.getValue< u32 >(pName, &code)) {
            return code;
        }
    } else {
        const char* pCode = nullptr;

        if (rIter.getValue< const char* >(pName, &pCode)) {
            const u32 code = pCodeTable->mIndex.find(pCode);
            return code == MR::NameIndex::cNoValue ? 0 : code;
        }
    }

    return 0;
}

u32 CollisionCode::getFloorCode(const TableIter& rIter) {
    return getCode(rIter, mFloorTable, "Floor_code");
}

u32 CollisionCode::getWallCode(const TableIter& rIter) {
    return getCode(rIter, mWallTable, "Wall_code");
}

u32 CollisionCode::getSoundCode(const TableIter& rIter) {
    return getCode(rIter, mSoundTable, "Sound_code");
}

u32 CollisionCode::getCameraCode(const TableIter& rIter) {
    return getCode(rIter, mCameraTable, "Camera_through");
}

u32 CollisionCode::getCameraID(const TableIter& rIter) {
    if (!rIter.isValid()) {
        return -1;
    }

    u32 id = -1;
    rIter.getValue< u32 >("camera_id", &id);

    return id;
}

