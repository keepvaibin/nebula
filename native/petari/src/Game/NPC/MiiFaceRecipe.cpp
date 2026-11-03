#include "Game/NPC/MiiFaceRecipe.hpp"
#include "Game/LiveActor/Nerve.hpp"

MiiFaceRecipe::MiiFaceRecipe(MiiDataSource dataSource, int index, MiiResolution resolution, u32 expressionFlag)
    : mDataSource(dataSource), mMiddleDB(nullptr), mIndex(index), mResolution(resolution), mExpressionFlag(expressionFlag) {
}

void MiiFaceRecipe::setDatabase(MiiMiddleDB* pMiddleDB) {
    mMiddleDB = pMiddleDB;
}

bool MiiFaceRecipe::hasExpression(MiiExpression expression) const {
    return (mExpressionFlag & (1 << expression)) != 0;
}

u32 MiiFaceRecipe::getModelBufferSize() const {
    return MiiGetModelBufferSize(mResolution, mExpressionFlag);
}

void MiiFaceRecipe::initFaceModel(MiiCharModel* pCharModel, void* pBuffer) const {
    MiiInitCharModel(pCharModel, mDataSource, mMiddleDB, mIndex, pBuffer, mResolution, mExpressionFlag);
}

MiiErrcode MiiFaceRecipe::makeIcon(void* pBuffer, int width, int height, NebulaColor8 bgColor) const {
    MiiIconSetting setting;

    setting.width = width;
    setting.height = height;
    setting.bgType = MiiIconBG_Direct;
    setting.bgColor = bgColor;
    setting.drawXluOnly = false;

    return MiiMakeIcon(pBuffer, mDataSource, mMiddleDB, mIndex, MiiExp_Normal, &setting);
}

MiiErrcode MiiFaceRecipe::makeIconWithFavoriteColor(void* pBuffer, int width, int height) const {
    MiiIconSetting setting;

    setting.width = width;
    setting.height = height;
    setting.bgType = MiiIconBG_Favorite;
    setting.drawXluOnly = false;

    return MiiMakeIcon(pBuffer, mDataSource, mMiddleDB, mIndex, MiiExp_Normal, &setting);
}
