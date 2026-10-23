#ifndef VMD_COVERAGE_FCNTL_H
#define VMD_COVERAGE_FCNTL_H
#include <stdint.h>
#include <fcntl.h>
typedef int64_t SceOff;
#define SCE_O_RDONLY O_RDONLY
#define SCE_SEEK_SET SEEK_SET
int sceIoOpen(const char *,int,int);
int sceIoClose(int);
int sceIoRead(int,void *,unsigned);
SceOff sceIoLseek(int,SceOff,int);
int sceIoRemove(const char *);
int sceIoRename(const char *,const char *);
#endif
