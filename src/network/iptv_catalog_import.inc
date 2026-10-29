/* Shared bounded local/remote playlist import. Never executes page scripts. */
#include <libxml/xmlreader.h>
#include <limits.h>
#define import_text vt_iptv_text_utf8
static char *import_trim(char *p){while(isspace((unsigned char)*p))p++;char *end=p+strlen(p);while(end>p&&isspace((unsigned char)end[-1]))*--end=0;return p;}
int vt_iptv_catalog_content_kind(const void *data,size_t size){
 const unsigned char *raw=data;
 if(raw&&((size>=12&&!memcmp(raw+4,"ftyp",4))||(size>=4&&!memcmp(raw,"\x1a\x45\xdf\xa3",4))||
    (size>=377&&raw[0]==0x47&&raw[188]==0x47&&raw[376]==0x47)))return VT_IPTV_FILE;
 char *text;size_t n;int r=import_text(data,size,&text,&n);if(r)return r;
 char *p=text;if(n>=3&&!memcmp(p,"\xef\xbb\xbf",3))p+=3;p=import_trim(p);
 int kind=VT_IPTV_LIST;
 if(!strncmp(p,"#EXTM3U",7)){
  for(char *line=p;line&&*line;){
   while(*line==' '||*line=='\t')line++;
   if(!strncmp(line,"#EXT-X-",7)){kind=VT_IPTV_HLS;break;}
   char *next=strchr(line,'\n');line=next?next+1:NULL;
  }
 }else if(*p=='<'){
  xmlTextReaderPtr reader=xmlReaderForMemory(p,(int)strlen(p),NULL,NULL,XML_PARSE_NONET|XML_PARSE_NOERROR|XML_PARSE_NOWARNING);
  int found=0;if(reader){while(xmlTextReaderRead(reader)==1){int type=xmlTextReaderNodeType(reader);
   if(type==XML_READER_TYPE_DOCUMENT_TYPE||type==XML_READER_TYPE_ENTITY_REFERENCE)break;
   if(type==XML_READER_TYPE_ELEMENT){const xmlChar *name=xmlTextReaderConstLocalName(reader);found=1;
    if(!xmlStrcasecmp(name,BAD_CAST "MPD"))kind=VT_IPTV_DASH;
    else if(xmlStrcasecmp(name,BAD_CAST "playlist")&&xmlStrcasecmp(name,BAD_CAST "asx"))kind=VT_IPTV_INVALID;
    break;}
  }xmlFreeTextReader(reader);}if(!found)kind=VT_IPTV_INVALID;
 }
 free(text);return kind;
}
static int import_add(VtIptvCatalog *out,const char *ref,const char *title,const char *base){
 VtIptvChannel item={0};char value[VT_IPTV_URL_MAX];
 if(strlen(ref)>=sizeof(value))return VT_IPTV_CATALOG_LINE_LIMIT;strcpy(value,ref);char *p=import_trim(value);
 const char *origin=!strncasecmp(p,"http://",7)||!strncasecmp(p,"https://",8)?p:base;
 int r=vt_iptv_resource_resolve(origin,p,item.url,sizeof(item.url));if(r)return r;
 for(unsigned i=0;i<out->count;i++)if(!strcmp(out->items[i].url,item.url))return 0;
 if(out->count==VT_IPTV_CHANNEL_MAX){out->truncated=1;return 0;}
 item.mode=vt_iptv_link_kind(item.url);snprintf(item.title,sizeof(item.title),"%s",title?title:"");
 if(!item.title[0]){const char *label=strrchr(item.url,'/');label=label?label+1:item.url;size_t len=strcspn(label,"?#");
  snprintf(item.title,sizeof(item.title),"%.*s",(int)(len<sizeof(item.title)?len:sizeof(item.title)-1),label);
  if(!item.title[0])snprintf(item.title,sizeof(item.title),"IPTV %u",out->count+1);
 }
 out->items[out->count++]=item;return 0;
}
static int import_xml(char *text,const char *base,VtIptvCatalog *out){
 xmlTextReaderPtr reader=xmlReaderForMemory(text,(int)strlen(text),NULL,NULL,XML_PARSE_NONET|XML_PARSE_NOERROR|XML_PARSE_NOWARNING);
 if(!reader)return VT_IPTV_INVALID;int result=0,reading,depth=-1;char title[160]="",logo[512]="",refs[8][VT_IPTV_URL_MAX];unsigned count=0;
 while((reading=xmlTextReaderRead(reader))==1){
  int type=xmlTextReaderNodeType(reader),d=xmlTextReaderDepth(reader);const xmlChar *name=xmlTextReaderConstLocalName(reader);
  if(d>32||type==XML_READER_TYPE_DOCUMENT_TYPE||type==XML_READER_TYPE_ENTITY_REFERENCE){result=VT_IPTV_INVALID;break;}
  if(type==XML_READER_TYPE_ELEMENT&&(!xmlStrcasecmp(name,BAD_CAST "track")||!xmlStrcasecmp(name,BAD_CAST "entry"))){depth=d;count=0;title[0]=logo[0]=0;}
  else if(depth>=0&&type==XML_READER_TYPE_ELEMENT){
   xmlChar *value=NULL;
   if(!xmlStrcasecmp(name,BAD_CAST "title")){value=xmlTextReaderReadString(reader);if(value)snprintf(title,sizeof(title),"%s",value);}
   else if(!xmlStrcasecmp(name,BAD_CAST "image")){value=xmlTextReaderReadString(reader);if(value){identity_logo(base,(char*)value,logo);xmlFree(value);}continue;}
   else if(!xmlStrcasecmp(name,BAD_CAST "location"))value=xmlTextReaderReadString(reader);
   else if(!xmlStrcasecmp(name,BAD_CAST "ref"))value=xmlTextReaderGetAttribute(reader,BAD_CAST "href");
   if(value){if(xmlStrcasecmp(name,BAD_CAST "title")){
    if(strlen((char*)value)>=VT_IPTV_URL_MAX){xmlFree(value);result=VT_IPTV_CATALOG_LINE_LIMIT;break;}
    if(count<8)snprintf(refs[count++],VT_IPTV_URL_MAX,"%s",value);else{xmlFree(value);result=VT_IPTV_CATALOG_LINE_LIMIT;break;}}
    xmlFree(value);}
  }else if(type==XML_READER_TYPE_END_ELEMENT&&d==depth){
   for(unsigned i=0;i<count&&!result;i++){unsigned before=out->count;result=import_add(out,refs[i],title,base);if(out->count>before&&logo[0])snprintf(out->items[before].logo,sizeof(out->items[before].logo),"%s",logo);}
   depth=-1;if(result)break;
  }
 }
 if(reading<0)result=VT_IPTV_INVALID;xmlFreeTextReader(reader);return result?result:out->count?0:VT_IPTV_INVALID;
}
int vt_iptv_catalog_parse(const void *data,size_t size,const char *base,VtIptvCatalog *out){
 if(!out)return VT_IPTV_INVALID;memset(out,0,sizeof(*out));char *text;size_t n;
 int r=import_text(data,size,&text,&n);if(r)return r;
 char *p=text;if(n>=3&&!memcmp(p,"\xef\xbb\xbf",3))p+=3;p=import_trim(p);
 if(!strncmp(p,"#EXTM3U",7))r=catalog_m3u_parse(p,strlen(p),base,out);
 else if(*p=='<')r=vt_iptv_catalog_content_kind(p,strlen(p))==VT_IPTV_LIST?import_xml(p,base,out):VT_IPTV_INVALID;
 else{
  char *cursor=p;unsigned ids[VT_IPTV_CHANNEL_MAX]={0},title_ids[VT_IPTV_CHANNEL_MAX]={0},title_count=0;char titles[VT_IPTV_CHANNEL_MAX][160];int pls=!strncasecmp(p,"[playlist]",10),shortcut=!strncasecmp(p,"[InternetShortcut]",18);
  while(*cursor&&!r){char *line=cursor,*next=strchr(cursor,'\n');if(next){*next=0;cursor=next+1;}else cursor+=strlen(cursor);
   line=import_trim(line);if(!*line||*line=='#'||*line==';'||*line=='[')continue;
   if(strlen(line)>4095){r=VT_IPTV_CATALOG_LINE_LIMIT;break;}
   if(pls||shortcut){
    char *equals=strchr(line,'=');if(!equals)continue;*equals=0;
    char *key=import_trim(line),*value=import_trim(equals+1);
    if(shortcut){if(!strcasecmp(key,"URL"))r=import_add(out,value,NULL,base);continue;}
    int file=!strncasecmp(key,"File",4),title=!strncasecmp(key,"Title",5);
    if(!file&&!title)continue;char *number=key+(file?4:5),*end=NULL;
    if(!isdigit((unsigned char)*number))continue;unsigned long index=strtoul(number,&end,10);
    if(*end||!index||index>UINT_MAX)continue;unsigned id=(unsigned)index;
    if(file){unsigned before=out->count;r=import_add(out,value,NULL,base);if(out->count>before){ids[before]=id;for(unsigned t=0;t<title_count;t++)if(title_ids[t]==id)snprintf(out->items[before].title,sizeof(out->items[before].title),"%s",titles[t]);}}
    else {for(unsigned i=0;i<out->count;i++)if(ids[i]==id)snprintf(out->items[i].title,sizeof(out->items[i].title),"%s",value);if(title_count<VT_IPTV_CHANNEL_MAX){title_ids[title_count]=id;snprintf(titles[title_count++],160,"%s",value);}}
   }
   else r=import_add(out,line,NULL,base);
  }
  if(!r&&!out->count)r=VT_IPTV_INVALID;
 }
 free(text);if(r)memset(out,0,sizeof(*out));return r;
}
