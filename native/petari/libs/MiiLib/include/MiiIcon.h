#ifndef RVL_FACE_LIBRARY_ICON_H
#define RVL_FACE_LIBRARY_ICON_H
#include "MiiTypes.h"
#include "MiiInternalTypes.h"
#include "MiiDatabase.h"
#include "MiiMiddleDatabase.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	MiiIconBG_Favorite = 0,
	MiiIconBG_Direct
} MiiIconBGType;

typedef struct {
	u16				width;
	u16				height;
	MiiIconBGType	bgType;
	NebulaColor8			bgColor;
	BOOL			drawXluOnly;
} MiiIconSetting;

MiiErrcode MiiMakeIcon(void *buf, MiiDataSource source, MiiMiddleDB *middleDB, u16 index, MiiExpression expression, const MiiIconSetting *setting);
void Mii_MakeIcon(void *buf, Mii_CharInfo* info, MiiExpression expression, const MiiIconSetting *setting);

#ifdef __cplusplus
}
#endif
#endif
