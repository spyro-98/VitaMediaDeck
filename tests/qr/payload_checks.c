/* Compiled after the actual scanner payload function by test-payload.py. */
static unsigned checks;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"QR contract failed line %d\n",__LINE__);return 1;}}while(0)
int main(void) {
 QrDecodeWorker *w=calloc(1,sizeof(*w));assert(w);
 struct quirc_data data={0};
 const char *accepted[]={"https://example.org/list.m3u", "HTTPS://example.org/LIST.M3U?sig=a%2Fb&x=1",
   " \t\xef\xbb\xbfHTTPS://example.org/list.m3u\r\n", "http://example.org/list.m3u#client-label"};
 const char *expected[]={"https://example.org/list.m3u", "https://example.org/LIST.M3U?sig=a%2Fb&x=1",
   "https://example.org/list.m3u", "http://example.org/list.m3u"};
 for(unsigned i=0;i<4;i++) {
  memset(w,0,sizeof(*w));data.payload_len=(int)strlen(accepted[i]);memcpy(data.payload,accepted[i],data.payload_len);
  CHECK(publish_qr_data(w,&data)==1);CHECK(w->result==1);CHECK(!strcmp(w->url,expected[i]));
  CHECK(!w->rejection);
 }
 memcpy(data.payload,accepted[0],strlen(accepted[0])+1);data.payload_len=(int)strlen(accepted[0])+1;
 memset(w,0,sizeof(*w));CHECK(publish_qr_data(w,&data));CHECK(!strcmp(w->url,accepted[0]));
 const char *rejected[]={"https://user:secret@example.org/list.m3u", "file:///tmp/list.m3u",
    "https://example.org/line\nbreak", "https://example.org/list.m3u\r\nX-Injected: value"};
 for(unsigned i=0;i<4;i++){
  memset(w,0,sizeof(*w));data.payload_len=(int)strlen(rejected[i]);memcpy(data.payload,rejected[i],data.payload_len);
  CHECK(!publish_qr_data(w,&data));CHECK(!w->result);CHECK(!w->url[0]);
 }
 memset(w,0,sizeof(*w));memcpy(data.payload,"https://example.org\0secret",26);data.payload_len=26;
 CHECK(!publish_qr_data(w,&data));CHECK(!w->result);
 memset(data.payload,'a',sizeof(data.payload));memcpy(data.payload,"https://example.org/",20);
 data.payload_len=sizeof(data.payload);memset(w,0,sizeof(*w));CHECK(!publish_qr_data(w,&data));CHECK(w->rejection==1);
 free(w);printf("QR payload handoff: %u actual-source checks passed; no camera emulation.\n",checks);return 0;
}
