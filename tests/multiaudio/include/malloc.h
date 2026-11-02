#pragma once
#include <stdlib.h>
static inline void *memalign(size_t alignment,size_t size){void *p=NULL;return posix_memalign(&p,alignment,size)?NULL:p;}
