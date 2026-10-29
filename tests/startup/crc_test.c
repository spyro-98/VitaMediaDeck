#include "common/crc32.h"
#include <assert.h>
#include <stdio.h>
static unsigned reference(const unsigned char *p,size_t n){
 unsigned v=~0u;while(n--){v^=*p++;for(int bit=0;bit<8;bit++)v=(v>>1)^(0xedb88320u&-(int)(v&1));}return ~v;
}
int main(void){
 assert(vt_crc32("",0)==0&&vt_crc32("123456789",9)==0xcbf43926u);
 unsigned char data[8192];for(unsigned i=0;i<sizeof(data);i++)data[i]=(unsigned char)(i*167u+i/13);
 for(unsigned offset=0;offset<32;offset++)for(unsigned n=0;n<sizeof(data)-offset;n+=37)
  assert(vt_crc32(data+offset,n)==reference(data+offset,n));
 puts("Cache CRC: IEEE check vector, empty data, unaligned ranges and original bitwise checksum compatibility passed.");
}
