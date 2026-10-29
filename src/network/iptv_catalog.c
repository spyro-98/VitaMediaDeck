#include "network/iptv_catalog.h"
#include "network/iptv_trace.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <math.h>
#include <libxml/HTMLparser.h>
#include <libxml/uri.h>
int vt_iptv_user_agent_valid(const char *text) {
    if(!text || strlen(text)>=VT_IPTV_USER_AGENT_MAX)return 0;
    for(const unsigned char *p=(const unsigned char*)text;*p;p++)if(*p<32 || *p>126)return 0;
    return 1;
}
static void search_key(const char *text,char *out,size_t capacity) {
    size_t used=0;
    for(const unsigned char *p=(const unsigned char *)text;*p && used+1<capacity;p++) {
        unsigned c=*p;
        if(c<128){if(isalnum(c))out[used++]=(char)tolower(c);}
        else if(c==0xc3 && p[1]>=0x80 && p[1]<=0xbf) {
            /* Fold common Latin accents as well as upper/lower case. */
            unsigned d=*++p;
            if(d>=0x80 && d<=0x9e && d!=0x97)d+=32;
            char folded=d>=0xa0&&d<=0xa5?'a':d==0xa7?'c':d>=0xa8&&d<=0xab?'e':
                d>=0xac&&d<=0xaf?'i':d==0xb1?'n':d>=0xb2&&d<=0xb6?'o':
                d>=0xb9&&d<=0xbc?'u':d==0xbd||d==0xbf?'y':0;
            if(folded)out[used++]=folded;
            else if(used+2<capacity){out[used++]=(char)c;out[used++]=(char)d;}
        } else out[used++]=(char)c;
    }
    out[used]=0;
}
/* Fold names like local search and compare digit runs by value without integer
 * conversion (Rai 2 precedes Rai 10). Equal names retain their source order. */
int vt_iptv_channel_compare_name(const VtIptvChannel *a,const VtIptvChannel *b){
 char left[320],right[320];search_key(a->title,left,sizeof(left));search_key(b->title,right,sizeof(right));
 const unsigned char *x=(const unsigned char*)left,*y=(const unsigned char*)right;
 if(!*x||!*y)return !!*y-!!*x;
 while(*x&&*y){
  if(isdigit(*x)&&isdigit(*y)){
   const unsigned char *ex=x,*ey=y;while(isdigit(*ex))ex++;while(isdigit(*ey))ey++;
   while(x<ex&&*x=='0')x++;
   while(y<ey&&*y=='0')y++;
   if(ex-x!=ey-y)return ex-x<ey-y?-1:1;
   int order=memcmp(x,y,(size_t)(ex-x));if(order)return order;
   x=ex;y=ey;
  }else{
   if(*x!=*y)return *x<*y?-1:1;
   x++;y++;
  }
 }
 return *x?1:*y?-1:0;
}
int vt_iptv_channel_matches(const VtIptvChannel *item,const char *query) {
    if(!item)return 0;
    char needle[256],key[VT_IPTV_URL_MAX];search_key(query?query:"",needle,sizeof(needle));
    if(!needle[0])return 1;
    const char *fields[]={item->title,item->group,item->channel_id};
    for(unsigned i=0;i<sizeof(fields)/sizeof(*fields);i++) {
        search_key(fields[i],key,sizeof(key));if(strstr(key,needle))return 1;
    }
    char public_url[VT_IPTV_URL_MAX];size_t n=strcspn(item->url,"?#");
    if(n>=sizeof(public_url))n=sizeof(public_url)-1;
    memcpy(public_url,item->url,n);public_url[n]=0;
    search_key(public_url,key,sizeof(key));return strstr(key,needle)!=NULL;
}
int vt_iptv_link_kind(const char *url) {
    const char *end = url + strcspn(url, "?#");
    const char *dot = end;
    while (dot > url && dot[-1] != '/' && dot[-1] != '.') dot--;
    if (dot == url || dot[-1] != '.') return VT_IPTV_HLS;
    size_t n = end - dot;
    if (n == 3 && !strncasecmp(dot, "m3u", n)) return VT_IPTV_LIST;
    const char *directories[]={"pls","xspf","asx","txt","url"};
    for(unsigned i=0;i<sizeof(directories)/sizeof(*directories);i++)if(n==strlen(directories[i])&&!strncasecmp(dot,directories[i],n))return VT_IPTV_LIST;
    if (n == 3 && !strncasecmp(dot, "mpd", n)) return VT_IPTV_DASH;
    const char *files[] = {"mp4", "m4v", "mkv", "mov", "webm", "avi", "ts"};
    for (unsigned i = 0; i < sizeof(files)/sizeof(*files); i++)
        if (n == strlen(files[i]) && !strncasecmp(dot, files[i], n)) return VT_IPTV_FILE;
    return VT_IPTV_HLS;
}
/* Extended M3U exporters use both quote styles, unquoted attributes and spaces
 * around '='. One parser is shared by the header and channel metadata. */
static int attribute(const char *line,const char *name,char *out,size_t cap){
 const char *p=line;size_t wanted=strlen(name);out[0]=0;
 while(*p){
  while(isspace((unsigned char)*p)||*p==',')p++;
  const char *key=p;while(*p&&*p!='='&&!isspace((unsigned char)*p))p++;
  size_t n=p-key;while(isspace((unsigned char)*p))p++;
  if(*p!='='){if(!*p)break;continue;}p++;
  while(isspace((unsigned char)*p))p++;
  char quote=*p=='\"'||*p=='\''?*p++:0;const char *value=p;
  while(*p&&(quote?*p!=quote:!isspace((unsigned char)*p)))p++;
  size_t length=p-value;
  if(n==wanted&&!strncasecmp(key,name,n)){
   if(length>=cap)return 0;
   memcpy(out,value,length);out[length]=0;return 1;
  }
  if(quote&&*p)p++;
 }
 return 0;
}
void vt_iptv_catalog_guide_reference(const char *base,const char *value,char out[512]){
 /* Preserve a bounded comma-separated list. Each fetch keeps its own URL
  * validation and cache; unavailable first guides can fall through to later ones. */
 out[0]=0;const char *p=value;unsigned count=0;
 while(*p&&count++<4){
  const char *end=strchr(p,',');if(!end)end=p+strlen(p);
  while(p<end&&isspace((unsigned char)*p))p++;
  while(end>p&&isspace((unsigned char)end[-1]))end--;
  char ref[512],url[512];size_t n=end-p;
  if(n&&n<sizeof(ref)){
   memcpy(ref,p,n);ref[n]=0;
   const char *origin=!strncasecmp(ref,"http://",7)||!strncasecmp(ref,"https://",8)?ref:base;
   if(!vt_iptv_resolve_url(origin,ref,url,sizeof(url))&&strlen(out)+strlen(url)+2<512){
    if(out[0])strcat(out,",");
    strcat(out,url);
   }
  }
  p=*end==','?end+1:end;if(!*p)break;
  while(*p&&isspace((unsigned char)*p))p++;
  if(*p==',')p++;
 }
}
static int guide_shift(const char *value,int *out){
 char *end;double hours=strtod(value,&end);
 if(end==value||*end||!isfinite(hours)||hours< -14||hours>14)return 0;
 *out=(int)lround(hours*3600);return 1;
}
static void metadata(char *line,VtIptvChannel *item,const char *base){
 char quote=0,*comma=NULL;
 for(char *p=line;*p;p++){
  if(quote){if(*p==quote)quote=0;}
  else if(*p=='\"'||*p=='\'')quote=*p;
  else if(*p==','){comma=p;break;}
 }
 if(comma){*comma++=0;snprintf(item->title,sizeof(item->title),"%s",comma);}
 char value[512];
 if(attribute(line,"tvg-logo",value,sizeof(value)))vt_iptv_resource_resolve(base,value,item->logo,sizeof(item->logo));
 if(attribute(line,"tvg-name",value,sizeof(value))){
  snprintf(item->guide_name,sizeof(item->guide_name),"%s",value);
  if(!item->title[0])snprintf(item->title,sizeof(item->title),"%s",value);
 }
 if(attribute(line,"group-title",value,sizeof(value)))snprintf(item->group,sizeof(item->group),"%s",value);
 if(attribute(line,"tvg-id",value,sizeof(value)))snprintf(item->channel_id,sizeof(item->channel_id),"%s",value);
 if(attribute(line,"tvg-shift",value,sizeof(value)))guide_shift(value,&item->guide_shift);
 if(attribute(line,"tvg-url",value,sizeof(value)))vt_iptv_catalog_guide_reference(base,value,item->guide_url);
 if(item->title[0])item->name_source=VT_IPTV_NAME_METADATA;
}
int vt_iptv_catalog_provider_guide(const char *playlist,char out[512]){
 out[0]=0;xmlURIPtr uri=xmlParseURI(playlist);if(!uri)return 0;
 const char *path=uri->path?strrchr(uri->path,'/'):NULL;
 char user[192]="",pass[192]="";
 if(path&&!strcasecmp(path,"/get.php")&&uri->query_raw){
  const char *p=uri->query_raw;
  while(*p){const char *end=strchr(p,'&');if(!end)end=p+strlen(p);
   const char *value=NULL;char *target=NULL;
   if(end-p>9&&!strncmp(p,"username=",9)){value=p+9;target=user;}
   if(end-p>9&&!strncmp(p,"password=",9)){value=p+9;target=pass;}
   if(target&&end-value<192){memcpy(target,value,end-value);target[end-value]=0;}
   p=*end?end+1:end;
  }
  if(user[0]&&pass[0]){
   char ref[512],checked[512];int n=snprintf(ref,sizeof(ref),"xmltv.php?username=%s&password=%s",user,pass);
   if(n>0&&(size_t)n<sizeof(ref)&&!vt_iptv_resolve_url(playlist,ref,checked,sizeof(checked)))snprintf(out,512,"%s",checked);
  }
 }
 xmlFreeURI(uri);return out[0]!=0;
}
static int catalog_m3u_parse(const void *bytes, size_t size, const char *base, VtIptvCatalog *out) {
    if (!out) return VT_IPTV_INVALID;
    memset(out, 0, sizeof(*out));
    if(!bytes||!size)return VT_IPTV_INVALID;
    if(size>VT_IPTV_CATALOG_LIMIT){IPTV_TRACE("IPTV catalog size limit: bytes=%u limit=%u\n",(unsigned)size,(unsigned)VT_IPTV_CATALOG_LIMIT);return VT_IPTV_CATALOG_BODY_LIMIT;}
    if(memchr(bytes,0,size))return VT_IPTV_INVALID;
    const char *data = bytes; size_t offset = 0; int header = 0;
    /* Channel lists (unlike strict HLS manifests) commonly have a UTF-8 BOM,
     * extended header attributes and whitespace from hand-edited exports. */
    if(size>=3 && !memcmp(data,"\xef\xbb\xbf",3))offset=3;
    VtIptvChannel pending = {0};char guide_url[512]="";int shift=0;
    while (offset < size) {
        size_t start = offset; while (offset < size && data[offset] != '\n') offset++;
        size_t n = offset - start; if (offset < size) offset++;
        while(n && isspace((unsigned char)data[start+n-1]))n--;
        while(n && (data[start]==' ' || data[start]=='\t')){start++;n--;}
        if (!n) continue;
        if(n>=4096){IPTV_TRACE("IPTV catalog line limit: bytes=%u offset=%u\n",(unsigned)n,(unsigned)start);return VT_IPTV_CATALOG_LINE_LIMIT;}
        char line[4096]; memcpy(line, data+start, n); line[n] = 0;
        if (!header) {
            if(strncmp(line,"#EXTM3U",7) || (line[7] && !isspace((unsigned char)line[7])))return VT_IPTV_INVALID;
            char value[512];
            if(attribute(line+7,"x-tvg-url",value,sizeof(value))||attribute(line+7,"url-tvg",value,sizeof(value))||attribute(line+7,"tvg-url",value,sizeof(value)))vt_iptv_catalog_guide_reference(base,value,guide_url);
            if(!guide_url[0])vt_iptv_catalog_provider_guide(base,guide_url);
            if(attribute(line+7,"tvg-shift",value,sizeof(value)))guide_shift(value,&shift);
            header=1;continue;
        }
        if (!strncmp(line, "#EXT-X-", 7)) return VT_IPTV_UNSUPPORTED;
        if (!strncmp(line, "#EXTINF:", 8)) { memset(&pending, 0, sizeof(pending)); pending.guide_shift=shift; metadata(line+8, &pending, base); }
        else if (!strncmp(line,"#EXTVLCOPT:http-user-agent=",27)) {
            /* This one declarative player identifier is required by public
             * broadcasters. Bound it and reject control/header injection. */
            if(!vt_iptv_user_agent_valid(line+27)) {
                IPTV_TRACE("IPTV catalog rejected: user-agent length=%u limit=%u\n",
                    (unsigned)strlen(line+27),(unsigned)VT_IPTV_USER_AGENT_MAX-1);
                return VT_IPTV_INVALID;
            }
            snprintf(pending.user_agent,sizeof(pending.user_agent),"%s",line+27);
        } else if (line[0] != '#') {
            /* An explicit HTTP channel in an HTTPS-hosted directory is a
             * separate resource, not a redirect downgrade of the playlist.
             * Relative URLs and all HLS redirects retain strict resolution. */
            const char *origin=!strncasecmp(line,"http://",7) || !strncasecmp(line,"https://",8)?line:base;
            if (!vt_iptv_resource_resolve(origin, line, pending.url, sizeof(pending.url))) {
                if(!pending.guide_url[0])snprintf(pending.guide_url,sizeof(pending.guide_url),"%s",guide_url);
                pending.mode = vt_iptv_link_kind(pending.url);
                if (!pending.title[0]) snprintf(pending.title, sizeof(pending.title), "IPTV %u", out->count+1);
                if (out->count < VT_IPTV_CHANNEL_MAX) out->items[out->count++] = pending;
                else out->truncated = 1;
            }
            memset(&pending, 0, sizeof(pending));
        }
    }
    return out->count ? 0 : VT_IPTV_INVALID;
}

static void catalog_target(const char *href,const char *base,char *out,size_t capacity,int *found,int *ambiguous){
 char target[VT_IPTV_URL_MAX];
 if(!href||vt_iptv_resolve_url(base,href,target,sizeof(target)))return;
 if(*found&&strcmp(out,target))*ambiguous=1;
 else if(strlen(target)<capacity){snprintf(out,capacity,"%s",target);*found=1;}
}
int vt_iptv_catalog_redirect(const void *bytes,size_t size,const char *base,char *out,size_t capacity){
 if(!out||!capacity)return 1;
 out[0]=0;
 if(!bytes||!size||size>1024u*1024u||!base||memchr(bytes,0,size))return 1;
 const unsigned char *first=bytes;size_t at=size>=3&&!memcmp(bytes,"\xef\xbb\xbf",3)?3:0;
 while(at<size&&isspace(first[at]))at++;
 if(at==size||first[at]!='<')return 1;
 htmlDocPtr doc=htmlReadMemory(bytes,(int)size,base,NULL,HTML_PARSE_NONET|HTML_PARSE_NOERROR|HTML_PARSE_NOWARNING|HTML_PARSE_COMPACT);
 if(!doc)return 1;
 xmlNode *root=xmlDocGetRootElement(doc),*node=root;unsigned visited=0;
 int found=0,ambiguous=0,meta_found=0,meta_ambiguous=0;char meta[VT_IPTV_URL_MAX]="";
 while(node&&visited++<32768){
  if(node->type==XML_ELEMENT_NODE&&!xmlStrcasecmp(node->name,BAD_CAST "a")){
   xmlChar *href=xmlGetProp(node,BAD_CAST "href");
   if(href){
    xmlURIPtr uri=xmlParseURI((const char*)href);size_t n=uri&&uri->path?strlen(uri->path):0;
    int playlist=n>=4&&(!strcasecmp(uri->path+n-4,".m3u")||(n>=5&&!strcasecmp(uri->path+n-5,".m3u8")));
    if(playlist)catalog_target((const char*)href,base,out,capacity,&found,&ambiguous);
    if(uri)xmlFreeURI(uri);
    xmlFree(href);
   }
  }else if(node->type==XML_ELEMENT_NODE&&!xmlStrcasecmp(node->name,BAD_CAST "meta")){
   xmlChar *equiv=xmlGetProp(node,BAD_CAST "http-equiv"),*content=xmlGetProp(node,BAD_CAST "content");
   if(equiv&&content&&!xmlStrcasecmp(equiv,BAD_CAST "refresh")){
    char *value=strchr((char*)content,';');
    if(value){value++;while(isspace((unsigned char)*value))value++;
     if(!strncasecmp(value,"url",3)){value+=3;while(isspace((unsigned char)*value))value++;
      if(*value=='='){value++;while(isspace((unsigned char)*value))value++;
       size_t n=strlen(value);while(n&&isspace((unsigned char)value[n-1]))value[--n]=0;
       if(n>=2&&(*value=='\''||*value=='"')&&value[n-1]==*value){value[n-1]=0;value++;}
       catalog_target(value,base,meta,sizeof(meta),&meta_found,&meta_ambiguous);
      }
     }
    }
   }
   if(equiv)xmlFree(equiv);
   if(content)xmlFree(content);
  }
  if(node->children){node=node->children;continue;}
  while(node&&node!=root&&!node->next)node=node->parent;
  if(!node||node==root)break;
  node=node->next;
 }
 xmlFreeDoc(doc);
 if(visited>=32768){out[0]=0;return 1;}
 /* A declarative redirect takes precedence over unrelated page links. */
 if(meta_found&&!meta_ambiguous&&strlen(meta)<capacity){snprintf(out,capacity,"%s",meta);return 0;}
 if(meta_ambiguous||!found||ambiguous){out[0]=0;return 1;}
 return 0;
}

static void identity_logo(const char *,const char *,char [512]);
#include "iptv_catalog_import.inc"

#include "iptv_identity.inc"
