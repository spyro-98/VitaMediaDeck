#ifndef VMD_COUNTRY_FLAGS_H
#define VMD_COUNTRY_FLAGS_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline uint32_t vt_flag_u32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
/* Reads one original PNG from a fixed-size index. No extraction or writes. */
static inline unsigned char *vt_country_flag_read(const char *pack, const char *code, size_t *size) {
    *size = 0;
    if (!code || strlen(code)!=2 || code[0]<'A' || code[0]>'Z' || code[1]<'A' || code[1]>'Z') return NULL;
    FILE *file = fopen(pack, "rb");
    if (!file) return NULL;
    unsigned char header[8]; unsigned char *data = NULL;
    unsigned slot = (code[0]-'A')*26 + code[1]-'A';
    if (fread(header,1,8,file)!=8 || memcmp(header,"VMDFLG1",8) ||
        fseek(file,8+slot*8,SEEK_SET) || fread(header,1,8,file)!=8) goto done;
    uint32_t offset = vt_flag_u32(header), length = vt_flag_u32(header+4);
    if (offset < 8+676*8 || length<8 || length>65536 || fseek(file,0,SEEK_END)) goto done;
    long end = ftell(file);
    if (end<0 || (uint64_t)offset+length>(uint64_t)end || fseek(file,offset,SEEK_SET)) goto done;
    data = malloc(length);
    if (!data) goto done;
    if (fread(data,1,length,file)!=length || memcmp(data,"\x89PNG\r\n\x1a\n",8)) {
        free(data); data=NULL;
    } else *size=length;
done:
    fclose(file); return data;
}
#endif
