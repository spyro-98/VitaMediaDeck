#ifndef VMD_TEST_IO_H
#define VMD_TEST_IO_H
#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
typedef int SceUID;
typedef unsigned int SceSize;
typedef int64_t SceOff;
#define SCE_O_RDONLY O_RDONLY
#define SCE_O_WRONLY O_WRONLY
#define SCE_O_CREAT O_CREAT
#define SCE_O_TRUNC O_TRUNC
#define SCE_SEEK_SET SEEK_SET
#define SCE_SEEK_CUR SEEK_CUR
#define SCE_SEEK_END SEEK_END
int sceIoOpen(const char *,int,int);
int sceIoRead(int,void *,unsigned);
int sceIoWrite(int,const void *,unsigned);
SceOff sceIoLseek(int,SceOff,int);
int sceIoClose(int);
int sceIoSyncByFd(int,int);
int sceIoRemove(const char *);
int sceIoRename(const char *,const char *);
int sceIoMkdir(const char *,int);
#endif
