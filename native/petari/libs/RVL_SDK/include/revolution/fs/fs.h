#ifndef FS_H
#define FS_H

#include "private/iosrestypes.h"
#include "private/iostypes.h"
#include "revolution/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef s32 ISFSError;
typedef void (*ISFSCallback)(ISFSError, void* ctxt);

s32 ISFS_Open(const u8*, u32);
s32 ISFS_OpenAsync(const u8*, u32, ISFSCallback, void*);

s32 ISFS_Read(s32, u8*, u32);
s32 ISFS_ReadAsync(IOSFd, u8*, u32, ISFSCallback, void*);
s32 ISFS_ShutdownAsync(ISFSCallback, void*);
s32 ISFS_ReadDir(const u8*, u8*, u32*);
s32 ISFS_ReadDirAsync(const u8*, u8*, u32*, ISFSCallback, void*);
s32 ISFS_Write(IOSFd, const u8*, u32);
s32 ISFS_WriteAsync(IOSFd, const u8*, u32, ISFSCallback, void*);

s32 ISFS_CreateFileAsync(const u8*, u32, u32, u32, u32, ISFSCallback, void*);

s32 ISFS_Seek(IOSFd, s32, u32);
s32 ISFS_SeekAsync(IOSFd, s32, u32, ISFSCallback, void*);

s32 ISFS_Close(IOSFd);
s32 ISFS_CloseAsync(IOSFd, ISFSCallback, void*);

s32 ISFS_GetUsage(const u8*, u32*, u32*);
s32 ISFS_GetAttrAsync(const u8*, IOSUid*, IOSGid*, u32*, u32*, u32*, u32*, ISFSCallback, void*);

s32 ISFS_CreateDirAsync(const u8*, u32, u32, u32, u32, ISFSCallback, void*);

s32 ISFS_RenameAsync(const u8*, const u8*, ISFSCallback, void*);

s32 ISFS_RenameAsync(const u8*, const u8*, ISFSCallback, void*);

s32 ISFS_Delete(const u8*);
s32 ISFS_DeleteAsync(const u8*, ISFSCallback, void*);

#ifdef __cplusplus
}
#endif

#endif  // FS_H
