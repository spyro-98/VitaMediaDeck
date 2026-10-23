#include "media/artwork_pixels.h"
#include "media/country_flags.h"
#include <assert.h>
static unsigned checks;
#define CHECK(x) do {checks++;assert(x);} while(0)
int main(int argc,char **argv) {
    CHECK(argc==3);
    unsigned char image[64*64*4];uint16_t output[64*64];
    for(unsigned y=0;y<64;y++)for(unsigned x=0;x<64;x++) {
        unsigned char *p=image+(y*64+x)*4;
        p[0]=y<8?255:0;p[1]=x<8?255:0;p[2]=y>=56?255:0;p[3]=255;
    }
    CHECK(!vt_artwork_fit_rgb565(image,256,64,64,4,output,64));
    CHECK(output[32]==0xf800); /* Top and bottom markers must NOT be cropped. */
    CHECK(output[63*64+32]==0x001f);
    CHECK(output[32*64]==0x07e0);
    CHECK(!vt_artwork_fit_rgb565(image,256,64,32,4,output,64));
    CHECK(output[32]==0 && output[16*64+32]==0xf800); /* Letterbox, no stretch. */
    CHECK(!vt_artwork_fit_rgb565(image,256,32,64,4,output,64));
    CHECK(output[32*64]==0 && output[16]==0xffe0);
    memset(image,255,sizeof(image));image[3]=0;
    CHECK(!vt_artwork_fit_rgb565(image,256,64,64,4,output,64));
    CHECK(output[0]==0 && output[1]==0xffff);
    CHECK(vt_artwork_fit_rgb565(image,1,64,64,4,output,64)<0);
    unsigned count=0;
    for(char a='A';a<='Z';a++)for(char b='A';b<='Z';b++) {
        char code[]={a,b,0},path[1024];size_t n;
        unsigned char *packed=vt_country_flag_read(argv[1],code,&n);
        snprintf(path,sizeof(path),"%s/%c%c.png",argv[2],a+32,b+32);
        FILE *file=fopen(path,"rb");
        CHECK(!!packed==!!file);
        if(file) {
            count++;CHECK(n>8 && n<=65536);
            for(size_t i=0;i<n;i++)CHECK(fgetc(file)==packed[i]);
            CHECK(fgetc(file)==EOF);fclose(file);
        }
        free(packed);
    }
    CHECK(count==251);
    const char *bad[]={"","../","I","ITA","it","I/","[T"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){size_t n=1;CHECK(!vt_country_flag_read(argv[1],bad[i],&n));CHECK(!n);}
    printf("Artwork: %u checks; uncropped square/portrait/landscape/alpha; %u packed flags byte-identical\n",checks,count);
}
