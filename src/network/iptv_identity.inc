/* Content identity shared by local files, network imports and player discovery. */
static int identity_copy(char *out,size_t cap,const char *value){
 if(!value)return 0;
 while(isspace((unsigned char)*value))value++;
 size_t n=strlen(value);while(n&&isspace((unsigned char)value[n-1]))n--;
 if(!n||n>=cap)return 0;
 for(size_t i=0;i<n;i++)if((unsigned char)value[i]<32||(unsigned char)value[i]==127)return 0;
 memcpy(out,value,n);out[n]=0;return 1;
}
int vt_iptv_channel_generic_name(const VtIptvChannel *v){
 if(!v)return 1;if(v->name_source==VT_IPTV_NAME_CUSTOM)return 0;
 if(v->name_source==VT_IPTV_NAME_AUTO||!v->title[0])return 1;
 const char *generic[]={"IPTV","M3U channel list","M3U playlist","Lista canali M3U","M3A channel list","Channel","Canale","Playlist","Service01","Service1","Service 1","Unknown","M3U-Kanalliste","Lista de canales M3U","Liste des chaînes M3U","M3Uチャンネルリスト","M3U 채널 목록","Lista de canais M3U","Список каналов M3U","M3U频道列表","M3U頻道列表"};
 for(unsigned i=0;i<sizeof(generic)/sizeof(*generic);i++)if(!strcasecmp(v->title,generic[i]))return 1;
 if(!strncasecmp(v->title,"Playlist · ",12)||!strncasecmp(v->title,"Lista · ",9)||!strncasecmp(v->title,"Channel · ",11)||!strncasecmp(v->title,"Canale · ",10))return 1;
 if(!strncasecmp(v->title,"service",7)){const char *p=v->title+7;while(*p==' ')p++;if(*p&&strspn(p,"0123456789")==strlen(p))return 1;}
 /* Legacy automatic labels predate the saved name-source field. */
 const char *prefixes[]={"Wiedergabeliste · ","Kanal · ","Lista de reproducción · ","Canal · ","Liste de lecture · ","プレイリスト · ","チャンネル · ","재생목록 · ","채널 · ","Lista de reprodução · ","Плейлист · ","Канал · ","播放列表·","频道·","播放清單·","頻道·"};
 for(unsigned i=0;i<sizeof(prefixes)/sizeof(*prefixes);i++)if(!strncmp(v->title,prefixes[i],strlen(prefixes[i])))return 1;
 const char *base=strrchr(v->url,'/');base=base?base+1:v->url;
 return !strcmp(v->title,base)||!strcmp(v->title,v->url);
}
int vt_iptv_channel_merge(VtIptvChannel *v,const VtIptvChannel *metadata){
 if(!v||!metadata)return 0;VtIptvChannel old=*v;
 if(vt_iptv_channel_generic_name(v)&&!vt_iptv_channel_generic_name(metadata)){
  if(identity_copy(v->title,sizeof(v->title),metadata->title))v->name_source=VT_IPTV_NAME_METADATA;
 }
 if(!v->logo[0]&&metadata->logo[0])identity_copy(v->logo,sizeof(v->logo),metadata->logo);
 if(!v->channel_id[0])identity_copy(v->channel_id,sizeof(v->channel_id),metadata->channel_id);
 if(!v->guide_name[0])identity_copy(v->guide_name,sizeof(v->guide_name),metadata->guide_name);
 if(!v->guide_url[0])identity_copy(v->guide_url,sizeof(v->guide_url),metadata->guide_url);
 if(!v->group[0])identity_copy(v->group,sizeof(v->group),metadata->group);
 return memcmp(&old,v,sizeof(old))!=0;
}
static void identity_logo(const char *base,const char *value,char out[512]){
 char clean[512],resolved[512];
 if(identity_copy(clean,sizeof(clean),value)&&!vt_iptv_resource_resolve(base,clean,resolved,sizeof(resolved)))snprintf(out,512,"%s",resolved);
}
static void identity_attributes(const char *line,const char *base,VtIptvChannel *v){
 char value[512];
 if(attribute(line,"tvg-name",value,sizeof(value)))identity_copy(v->title,sizeof(v->title),value);
 if(attribute(line,"tvg-id",value,sizeof(value)))identity_copy(v->channel_id,sizeof(v->channel_id),value);
 if(attribute(line,"tvg-logo",value,sizeof(value)))identity_logo(base,value,v->logo);
 if(attribute(line,"tvg-url",value,sizeof(value))||attribute(line,"x-tvg-url",value,sizeof(value))||attribute(line,"url-tvg",value,sizeof(value)))vt_iptv_catalog_guide_reference(base,value,v->guide_url);
}
int vt_iptv_catalog_metadata(const void *data,size_t size,const char *base,VtIptvChannel *item){
 if(!item||!base||size>VT_IPTV_PLAYLIST_LIMIT)return 0;
 char *text=NULL;size_t n=0;if(vt_iptv_text_utf8(data,size,&text,&n))return 0;
 VtIptvChannel metadata={.name_source=VT_IPTV_NAME_METADATA};char *start=text;
 if(n>=3&&!memcmp(start,"\xef\xbb\xbf",3))start+=3;
 while(isspace((unsigned char)*start))start++;
 if(!strncmp(start,"#EXTM3U",7)){
  char *cursor=start;
  while(cursor&&*cursor){char *line=cursor,*next=strchr(line,'\n');if(next)*next++=0;cursor=next;
   while(*line==' '||*line=='\t')line++;
   size_t len=strlen(line);if(len&&line[len-1]=='\r')line[len-1]=0;
   if(!strncmp(line,"#EXTM3U",7))identity_attributes(line+7,base,&metadata);
   else if(!strncmp(line,"#PLAYLIST:",10)&&!metadata.title[0])identity_copy(metadata.title,sizeof(metadata.title),line+10);
   else if(!strncmp(line,"#EXT-X-SESSION-DATA:",20)){
    char id[256],value[512];
    if(attribute(line+20,"DATA-ID",id,sizeof(id))&&attribute(line+20,"VALUE",value,sizeof(value))){
     if(strstr(id,"channel.name")||strstr(id,"channel-name")||strstr(id,"service.name")||strstr(id,"service-name")||!strcmp(id,"tvg-name"))identity_copy(metadata.title,sizeof(metadata.title),value);
     else if(strstr(id,"channel.logo")||strstr(id,"channel-logo")||!strcmp(id,"tvg-logo"))identity_logo(base,value,metadata.logo);
    }
   }
  }
 }else if(*start=='<'){
  xmlTextReaderPtr reader=xmlReaderForMemory(start,(int)strlen(start),NULL,NULL,XML_PARSE_NONET|XML_PARSE_NOERROR|XML_PARSE_NOWARNING);
  int depth=-1,valid=1,read_status=0;
  if(reader){while((read_status=xmlTextReaderRead(reader))==1){int type=xmlTextReaderNodeType(reader),d=xmlTextReaderDepth(reader);const xmlChar *name=xmlTextReaderConstLocalName(reader);
   if(d>24||type==XML_READER_TYPE_DOCUMENT_TYPE||type==XML_READER_TYPE_ENTITY_REFERENCE){valid=0;break;}
   if(type==XML_READER_TYPE_ELEMENT&&d==0&&xmlStrcmp(name,BAD_CAST "MPD")){valid=0;break;}
   if(type==XML_READER_TYPE_ELEMENT&&!xmlStrcmp(name,BAD_CAST "ProgramInformation"))depth=d;
   else if(type==XML_READER_TYPE_END_ELEMENT&&d==depth)depth=-1;
   else if(depth>=0&&type==XML_READER_TYPE_ELEMENT&&!xmlStrcmp(name,BAD_CAST "Title")){
    xmlChar *value=xmlTextReaderReadString(reader);if(value){identity_copy(metadata.title,sizeof(metadata.title),(char*)value);xmlFree(value);}
   }
  }if(read_status<0)valid=0;xmlFreeTextReader(reader);}if(!valid)memset(&metadata,0,sizeof(metadata));
 }
 free(text);return vt_iptv_channel_merge(item,&metadata);
}
