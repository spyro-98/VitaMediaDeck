#include "network/online_audio.h"
#include "common/text_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <jansson.h>
#include <libxml/parser.h>
#include <libxml/xmlreader.h>
#include <libxml/uri.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <vita_https.h>

#define RESPONSE_LIMIT (2 * 1024 * 1024)
#define SAVED_PATH "ux0:data/VitaMediaDeck/online-audio.json"
#define SAVED_TEMP "ux0:data/VitaMediaDeck/online-audio.tmp"
#define SAVED_BACKUP "ux0:data/VitaMediaDeck/online-audio.bak"

int vt_online_url_valid(const char *url) {
	if (!url || strlen(url) >= 512) return 0;
	const char *host = !strncmp(url, "https://", 8) ? url + 8
	                 : !strncmp(url, "http://", 7) ? url + 7 : NULL;
	if (!host || !*host || *host == '/' || *host == '?' || *host == ':') return 0;
	for (const char *p = url; *p; p++)
		if ((unsigned char)*p <= 32 || *p == '\\' || *p == '#') return 0;
	const char *end = strchr(host, '/');
	const char *auth = strchr(host, '@');
	return !auth || (end && auth > end);
}

typedef struct { char *data; size_t size, capacity, limit; int limit_hit; } Response;
static size_t receive(const void *data, size_t size, void *ctx) {
	Response *r = ctx;
	if (size > r->limit - r->size) { r->limit_hit = 1; return 0; }
	if (r->size + size + 1 > r->capacity) {
		size_t capacity = r->capacity ? r->capacity : 65536;
		while (capacity < r->size + size + 1) capacity *= 2;
		if (capacity > r->limit + 1) capacity = r->limit + 1;
		char *next = realloc(r->data, capacity);
		if (!next) return 0;
		r->data = next; r->capacity = capacity;
	}
	memcpy(r->data + r->size, data, size);
	r->size += size;
	r->data[r->size] = 0;
	return size;
}

static char *fetch_limited(const char *url, volatile int *cancel, size_t *length, size_t limit) {
	if (!vt_online_url_valid(url)) {
		log_printf("online HTTP: rejected URL (length=%u)\n", (unsigned)(url ? strlen(url) : 0));
		return NULL;
	}
	Response result = {.data = malloc(1), .capacity = 1, .limit = limit};
	if (!result.data) return NULL;
	result.data[0] = 0;
	VitaHttpsClientConfig options = {.user_agent = "VitaMediaDeck/1.3.0",
	    .allow_http = 1, .connect_timeout_ms = 10000, .request_timeout_ms = 20000};
	VitaHttpsClient *client = vita_https_client_create(&options);
	if (!client) { free(result.data); return NULL; }
	VitaHttpsRequest request = {.method = "GET", .url = url, .write = receive,
	    .write_opaque = &result, .cancel_flag = cancel};
	VitaHttpsResponse response = {0};
	int code = vita_https_perform(client, &request, &response);
	vita_https_client_destroy(client);
	/* Do not log URLs, redirects or payloads: podcast links may contain tokens. */
	log_printf("online HTTP: result=%d status=%ld bytes=%u limit_hit=%d cancelled=%d\n",
	    code, response.status_code, (unsigned)result.size, result.limit_hit,
	    cancel && *cancel);
	if (code < 0 || response.status_code != 200) { free(result.data); return NULL; }
	*length = result.size;
	return result.data;
}

static char *fetch(const char *url, volatile int *cancel, size_t *length) {
	return fetch_limited(url, cancel, length, RESPONSE_LIMIT);
}

static void add(VtOnlineQuery *q, const char *title, const char *url,
	             const char *id, int kind) {
	if (q->count >= VT_ONLINE_MAX || !vt_online_url_valid(url)) return;
	for (int i = 0; i < q->count; i++) if (!strcmp(q->items[i].url, url)) return;
	VtOnlineItem *item = &q->items[q->count++];
	memset(item, 0, sizeof(*item));
	snprintf(item->title, sizeof(item->title), "%s", title && *title ? title : url);
	snprintf(item->url, sizeof(item->url), "%s", url);
	snprintf(item->id, sizeof(item->id), "%s", id ? id : "");
	item->kind = kind;
}

static xmlNode *child(xmlNode *node, const char *name) {
	for (xmlNode *n = node ? node->children : NULL; n; n = n->next)
		if (n->type == XML_ELEMENT_NODE && !xmlStrcmp(n->name, BAD_CAST name)) return n;
	return NULL;
}

static void node_text(xmlNode *node, char *out, size_t cap) {
	xmlChar *text=xmlNodeGetContent(node);size_t n=0;int tag=0;
	for(const unsigned char *p=text;p && *p && n+1<cap;p++) {
		if(*p=='<'){tag=1;continue;}if(*p=='>'){tag=0;continue;}
		if(!tag)out[n++]=*p<32?' ':(char)*p;
	}out[n]=0;xmlFree(text);
}
static void image_artwork(xmlNode *node, const char *base, char *out, size_t cap) {
	xmlChar *link = xmlGetProp(node, BAD_CAST "href");
	if (!link) link = xmlNodeGetContent(child(node, "url"));
	xmlChar *absolute = link ? xmlBuildURI(link, BAD_CAST base) : NULL;
	if (absolute && vt_online_url_valid((char *)absolute))
		snprintf(out, cap, "%s", (char *)absolute);
	xmlFree(absolute); xmlFree(link);
}
static void node_artwork(xmlNode *node, const char *base, char *out, size_t cap) {
	for(xmlNode *n=node?node->children:NULL;n;n=n->next) {
		if(xmlStrcmp(n->name,BAD_CAST "image"))continue;
		image_artwork(n, base, out, cap); if(out[0])return;
	}
}
static void feed_item(xmlNode *node, const char *url, VtOnlineQuery *q, unsigned *eligible) {
		xmlNode *enclosure = child(node, "enclosure");
		xmlChar *link = xmlGetProp(enclosure, BAD_CAST "url");
		xmlChar *mime = xmlGetProp(enclosure, BAD_CAST "type");
		xmlChar *title = xmlNodeGetContent(child(node, "title"));
		xmlChar *guid = xmlNodeGetContent(child(node, "guid"));
		xmlChar *absolute = link ? xmlBuildURI(link, BAD_CAST url) : NULL;
		int before=q->count;
		if (absolute && vt_online_url_valid((char*)absolute) && (!mime || !xmlStrncmp(mime, BAD_CAST "audio/", 6))) {
			if((*eligible)++ >= q->offset) {
				if(*eligible>q->offset+VT_ONLINE_MAX)q->has_more=1;
				else add(q, (char *)title, (char *)absolute, (char *)guid, VT_ONLINE_EPISODE);
			}
		}
		if(q->count>before) {
			VtOnlineItem *item=&q->items[before];
			node_text(child(node,"author"),item->author,sizeof(item->author));
			if(!item->author[0])node_text(child(node,"creator"),item->author,sizeof(item->author));
			node_text(child(node,"description"),item->description,sizeof(item->description));
			if(!item->description[0])node_text(child(node,"summary"),item->description,sizeof(item->description));
			node_artwork(node,url,item->artwork_url,sizeof(item->artwork_url));
		}
		xmlFree(link); xmlFree(mime); xmlFree(title); xmlFree(guid); xmlFree(absolute);
}

int vt_online_parse_feed(const char *data, size_t size, const char *url, VtOnlineQuery *q) {
	if (!q) return -1;
	q->count = q->has_more = 0;
	if (!data || !size || size > VT_ONLINE_FEED_LIMIT || q->offset > 1000000) return -1;
	xmlTextReader *reader = xmlReaderForMemory(data, (int)size, url, NULL,
	    XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
	if (!reader) return -1;
	/* No DTD loading, entity substitution, preservation or huge-document mode.
	 * Expand only one direct channel child at a time; Next releases that tree. */
	xmlTextReaderSetParserProp(reader, XML_PARSER_LOADDTD, 0);
	xmlTextReaderSetParserProp(reader, XML_PARSER_SUBST_ENTITIES, 0);
	VtOnlineItem defaults = {0};
	unsigned eligible = 0;
	int root_seen = 0, channel_seen = 0, in_channel = 0;
	int status = xmlTextReaderRead(reader);
	while (status == 1) {
		if (q->cancel || (q->cancel_parent && *q->cancel_parent)) { status = -1; break; }
		int type = xmlTextReaderNodeType(reader), depth = xmlTextReaderDepth(reader);
		const xmlChar *name = xmlTextReaderConstLocalName(reader);
		if (type == XML_READER_TYPE_DOCUMENT_TYPE || type == XML_READER_TYPE_ENTITY_REFERENCE) { status = -1; break; }
		if (type == XML_READER_TYPE_ELEMENT && depth == 0) {
			if (root_seen || xmlStrcmp(name, BAD_CAST "rss")) { status = -1; break; }
			root_seen = 1;
		} else if (type == XML_READER_TYPE_ELEMENT && depth == 1 && !xmlStrcmp(name, BAD_CAST "channel")) {
			if (!root_seen || channel_seen) { status = -1; break; }
			channel_seen = in_channel = 1;
		} else if (type == XML_READER_TYPE_END_ELEMENT && depth == 1) {
			in_channel = 0;
		} else if (type == XML_READER_TYPE_ELEMENT && depth == 2 && in_channel) {
			xmlNode *node = xmlTextReaderExpand(reader);
			if (!node) { status = -1; break; }
			if (!xmlStrcmp(node->name, BAD_CAST "item")) feed_item(node, url, q, &eligible);
			else if (!xmlStrcmp(node->name, BAD_CAST "author")) node_text(node, defaults.author, sizeof(defaults.author));
			else if (!xmlStrcmp(node->name, BAD_CAST "description")) node_text(node, defaults.description, sizeof(defaults.description));
			else if (!xmlStrcmp(node->name, BAD_CAST "image")) image_artwork(node, url, defaults.artwork_url, sizeof(defaults.artwork_url));
			status = xmlTextReaderNext(reader);
			continue;
		}
		status = xmlTextReaderRead(reader);
	}
	xmlFreeTextReader(reader);
	if (status < 0 || !channel_seen) { q->count = q->has_more = 0; return -1; }
	/* Feed metadata may legally follow the episodes. Apply fallbacks only after
	 * parsing, without retaining the complete feed tree. */
	for (int i = 0; i < q->count; i++) {
		VtOnlineItem *item = &q->items[i];
		if (!item->author[0]) snprintf(item->author, sizeof(item->author), "%s", defaults.author);
		if (!item->description[0]) snprintf(item->description, sizeof(item->description), "%s", defaults.description);
		if (!item->artwork_url[0]) snprintf(item->artwork_url, sizeof(item->artwork_url), "%s", defaults.artwork_url);
		snprintf(item->detail, sizeof(item->detail), "%s", item->author);
	}
	return 0;
}

static const char *json_text(json_t *object, const char *key) {
	const char *value = json_string_value(json_object_get(object, key));
	return value ? value : "";
}

static int fetch_query_raw(void *opaque) {
	VtOnlineQuery *q = opaque;
	if (!q || (q->country[0] && (q->country[2] != 0 ||
	    q->country[0] < 'A' || q->country[0] > 'Z' ||
	    q->country[1] < 'A' || q->country[1] > 'Z')) ||
	    q->kind < VT_ONLINE_RADIO || q->kind > VT_ONLINE_SHOW ||
	    q->offset > 1000000) return -1;
	q->count = 0;
	q->has_more = 0;
	volatile int *cancel = q->cancel_parent ? q->cancel_parent : &q->cancel;
	unsigned batch = q->batch_size ? q->batch_size : VT_ONLINE_MAX;
	if (q->kind == VT_ONLINE_EPISODE) {
		size_t size = 0;
		char *data = fetch_limited(q->query, cancel, &size, VT_ONLINE_FEED_LIMIT);
		int result = data ? vt_online_parse_feed(data, size, q->query, q) : -1;
		log_printf("podcast feed: fetched=%d bytes=%u parse_result=%d episodes=%d offset=%u more=%d cancelled=%d\n",
		    data != NULL, (unsigned)size, result, q->count, q->offset,
		    q->has_more, cancel && *cancel);
		free(data); return result;
	}
	char *escaped = vita_https_escape(q->query, strlen(q->query));
	if (!escaped) return -1;
	char url[512];
	int result = -1;
	if(q->kind==VT_ONLINE_SHOW) {
		snprintf(url,sizeof(url),"https://rss.marketingtools.apple.com/api/v2/%c%c/podcasts/top/100/podcasts.json",
		    q->country[0]?tolower(q->country[0]):'u',q->country[0]?tolower(q->country[1]):'s');
		size_t size=0;char *data=fetch(url,cancel,&size);json_t *root=data?json_loadb(data,size,0,NULL):NULL;free(data);
		json_t *array=json_object_get(json_object_get(root,"feed"),"results"),*entry;size_t i;
		json_array_foreach(array,i,entry) {
			if(i<q->offset||i>=q->offset+VT_ONLINE_MAX)continue;
			int before=q->count;add(q,json_text(entry,"name"),json_text(entry,"url"),json_text(entry,"id"),VT_ONLINE_SHOW);
			if(q->count>before){VtOnlineItem *item=&q->items[before];
				snprintf(item->author,sizeof(item->author),"%s",json_text(entry,"artistName"));
				snprintf(item->artwork_url,sizeof(item->artwork_url),"%s",json_text(entry,"artworkUrl100"));
				json_t *genre=json_array_get(json_object_get(entry,"genres"),0);
				snprintf(item->detail,sizeof(item->detail),"%s / %s",item->author,json_text(genre,"name"));}
		}
		q->has_more=json_array_size(array)>q->offset+VT_ONLINE_MAX;result=json_is_array(array)?0:-1;json_decref(root);
	} else if (q->kind == VT_ONLINE_FEED) {
		int n = snprintf(url, sizeof(url), "https://itunes.apple.com/search?media=podcast&entity=podcast&limit=200&country=%s&term=%s",
		    q->country[0] ? q->country : "US", escaped);
		if (n >= (int)sizeof(url)) goto done;
		size_t size = 0; char *data = fetch(url, cancel, &size);
		json_t *root = data ? json_loadb(data, size, 0, NULL) : NULL;
		free(data);
		json_t *array = json_object_get(root, "results"), *entry;
		size_t i;
		json_array_foreach(array, i, entry) {
			if (i < q->offset || i >= q->offset + batch) continue;
			int before = q->count;
			add(q, json_text(entry, "collectionName"), json_text(entry, "feedUrl"), "", VT_ONLINE_FEED);
			if (q->count > before) {
				VtOnlineItem *item=&q->items[before];
				snprintf(item->author,sizeof(item->author),"%s",json_text(entry,"artistName"));
				const char *art=json_text(entry,"artworkUrl600");if(!*art)art=json_text(entry,"artworkUrl100");
				snprintf(item->artwork_url,sizeof(item->artwork_url),"%s",art);
				snprintf(item->description,sizeof(item->description),"%s",json_text(entry,"description"));
				snprintf(q->items[before].detail, sizeof(q->items[before].detail), "%s / %s",
				    json_text(entry, "artistName"), json_text(entry, "primaryGenreName"));
			}
		}
		q->has_more = json_array_size(array) > q->offset + batch;
		result = json_is_array(array) ? 0 : -1;
		json_decref(root);
	} else {
		/* Discover server names first; bootstrap alternatives are only fallbacks. */
		char servers[8][96] = {"all.api.radio-browser.info", "de1.api.radio-browser.info"};
		int server_count = 2;
		size_t bytes = 0;
		char *directory = fetch("https://all.api.radio-browser.info/json/servers", cancel, &bytes);
		json_t *list = directory ? json_loadb(directory, bytes, 0, NULL) : NULL;
		free(directory);
		json_t *entry; size_t i;
		json_array_foreach(list, i, entry) {
			const char *name = json_text(entry, "name");
			size_t length = strlen(name);
			if (server_count == 8) break;
			const size_t suffix = sizeof(".api.radio-browser.info") - 1;
			if (length <= suffix || length >= sizeof(servers[0]) ||
			    strcmp(name + length - suffix, ".api.radio-browser.info")) continue;
			if (strspn(name, "abcdefghijklmnopqrstuvwxyz0123456789-.") != length) continue;
			int duplicate = 0;
			for (int j = 0; j < server_count; j++)
				if (!strcmp(name, servers[j])) duplicate = 1;
			if (duplicate) continue;
			snprintf(servers[server_count++], sizeof(servers[0]), "%s", name);
		}
		json_decref(list);
		for (int server = server_count - 1; server >= 0 && !*cancel; server--) {
			int n = q->kind == VT_ONLINE_TAG
			    ? snprintf(url,sizeof(url),"https://%s/json/tags?hidebroken=true&limit=64&offset=%u&order=stationcount&reverse=true",servers[server],q->offset)
			    : q->kind == VT_ONLINE_COUNTRY
			    ? snprintf(url, sizeof(url), "https://%s/json/countries/%s?hidebroken=true&limit=64&offset=%u&order=name",
			        servers[server], escaped, q->offset)
			    : snprintf(url, sizeof(url), "https://%s/json/stations/search?%s=%s&countrycode=%s&codec=MP3&hidebroken=true&limit=%u&offset=%u&order=%s&reverse=%s",
			        servers[server], q->search_field == 1 ? "tag" : q->search_field == 2 ? "state" : "name",
			        escaped, q->country, batch, q->offset,q->browse?"clickcount":"name",q->browse?"true":"false");
			if (n >= (int)sizeof(url)) break;
			size_t size = 0; char *data = fetch(url, cancel, &size);
			json_t *array = data ? json_loadb(data, size, 0, NULL) : NULL;
			free(data);
			if (!json_is_array(array)) { json_decref(array); continue; }
			q->has_more = json_array_size(array) >= batch;
			json_array_foreach(array, i, entry) {
				if(q->kind==VT_ONLINE_TAG) {
					if(q->count==VT_ONLINE_MAX)break;
					const char *name=json_text(entry,"name");if(!*name)continue;
					VtOnlineItem *item=&q->items[q->count++];memset(item,0,sizeof(*item));item->kind=VT_ONLINE_TAG;
					snprintf(item->title,sizeof(item->title),"%s",name);snprintf(item->id,sizeof(item->id),"%s",name);continue;
				}
				if (q->kind == VT_ONLINE_COUNTRY) {
					const char *code = json_text(entry, "iso_3166_1");
					if (strlen(code) != 2 || strspn(code, "ABCDEFGHIJKLMNOPQRSTUVWXYZ") != 2 ||
					    q->count == VT_ONLINE_MAX) continue;
					VtOnlineItem *item = &q->items[q->count++];
					memset(item, 0, sizeof(*item)); item->kind = VT_ONLINE_COUNTRY;
					snprintf(item->id, sizeof(item->id), "%s", code);
					snprintf(item->title, sizeof(item->title), "%s", json_text(entry, "name"));
					snprintf(item->detail, sizeof(item->detail), "%s", code);
					continue;
				}
				if (json_integer_value(json_object_get(entry, "hls"))) continue;
				if (strcmp(json_text(entry, "codec"), "MP3")) continue;
				if (q->country[0] && strcmp(json_text(entry, "countrycode"), q->country)) continue;
				const char *resolved = json_text(entry, "url_resolved");
				int before = q->count;
				add(q, json_text(entry, "name"), *resolved ? resolved : json_text(entry, "url"),
				    json_text(entry, "stationuuid"), VT_ONLINE_RADIO);
				if (q->count > before) {
					VtOnlineItem *item=&q->items[before];
					snprintf(item->artwork_url,sizeof(item->artwork_url),"%s",json_text(entry,"favicon"));
					snprintf(item->description,sizeof(item->description),"%s",json_text(entry,"tags"));
					snprintf(q->items[before].detail, sizeof(q->items[before].detail), "%s / %s / MP3",
					    json_text(entry, "countrycode"), json_text(entry, "state"));
				}
			}
			json_decref(array); result = 0; break;
		}
	}
done:
	vita_https_free(escaped);
	return result;
}

void vt_online_search_normalize(const char *in, char *spaced, char *compact, size_t cap) {
	if (!cap) return;
	size_t s=0,c=0;
	for(size_t i=0;in && in[i];i++) {
		unsigned char ch=in[i], prev=i?(unsigned char)in[i-1]:0;
		int boundary=isspace(ch)||ch=='_'||ch=='-' ||
		    (isupper(ch) && (islower(prev)||isdigit(prev)||(isupper(prev)&&islower((unsigned char)in[i+1]))));
		if(boundary && s && spaced[s-1]!=' ' && s+1<cap)spaced[s++]=' ';
		if(isspace(ch)||ch=='_'||ch=='-')continue;
		if(s+1<cap)spaced[s++]=(char)tolower(ch);
		if(c+1<cap)compact[c++]=(char)tolower(ch);
	}
	while(s && spaced[s-1]==' ')s--;
	spaced[s]=compact[c]=0;
}

int vt_online_fetch_query(void *ctx) {
	VtOnlineQuery *q=ctx;
	if(!q)return -1;
	if(q->browse||(q->kind!=VT_ONLINE_RADIO && q->kind!=VT_ONLINE_FEED)||!q->query[0])return fetch_query_raw(q);
	char spaced[512],compact[512];vt_online_search_normalize(q->query,spaced,compact,sizeof(spaced));
	int variants=q->kind==VT_ONLINE_RADIO?4:2, success=0;
	VtOnlineQuery *part=calloc(1,sizeof(*part));if(!part)return -1;
	q->count=q->has_more=0;
	for(int v=0;v<variants && !q->cancel;v++) {
		part->kind=q->kind;part->cancel=q->cancel;part->cancel_parent=&q->cancel;
		memcpy(part->country,q->country,sizeof(part->country));
		part->offset=q->offset/(unsigned)variants;part->batch_size=VT_ONLINE_MAX/(unsigned)variants;
		part->search_field=v==2?1:v==3?2:0;
		snprintf(part->query,sizeof(part->query),"%s",v==1?compact:spaced);
		/* Share cancellation while requests run, not a one-time copied flag. */
		part->cancel=q->cancel;
		if(fetch_query_raw(part)<0)continue;
		success=1;q->has_more|=part->has_more;
		for(int i=0;i<part->count && q->count<VT_ONLINE_MAX;i++) {
			int duplicate=0;for(int j=0;j<q->count;j++)if(!strcmp(q->items[j].url,part->items[i].url))duplicate=1;
			if(!duplicate)q->items[q->count++]=part->items[i];
		}
	}
	free(part);return q->cancel || !success ? -1 : 0;
}

int vt_online_load_saved(VtOnlineItem *out, int capacity) {
	SceIoStat status;
	if (!out || capacity <= 0) return 0;
	const char *path = SAVED_PATH;
	if (sceIoGetstat(path, &status) < 0) {
		path = SAVED_BACKUP;
		if (sceIoGetstat(path, &status) < 0) return 0;
	}
	if (status.st_size > 512 * 1024) return -1;
	json_t *array = json_load_file(path, 0, NULL), *entry;
	if (!json_is_array(array)) { json_decref(array); return -1; }
	size_t i; int count = 0;
	json_array_foreach(array, i, entry) {
		if (count >= capacity) break;
		const char *url = json_text(entry, "url");
		int kind = (int)json_integer_value(json_object_get(entry, "kind"));
		if (!vt_online_url_valid(url) || (kind != VT_ONLINE_RADIO && kind != VT_ONLINE_FEED)) continue;
		VtOnlineItem *item = &out[count++];
		memset(item, 0, sizeof(*item));
		snprintf(item->title, sizeof(item->title), "%s", json_text(entry, "title"));
		snprintf(item->url, sizeof(item->url), "%s", url);
		snprintf(item->id, sizeof(item->id), "%s", json_text(entry, "id"));
		snprintf(item->artwork_url,sizeof(item->artwork_url),"%s",json_text(entry,"artwork"));
		snprintf(item->author,sizeof(item->author),"%s",json_text(entry,"author"));
		snprintf(item->description,sizeof(item->description),"%s",json_text(entry,"description"));
		snprintf(item->detail,sizeof(item->detail),"%s",json_text(entry,"detail"));
		item->kind = kind;
	}
	json_decref(array); return count;
}

int vt_online_save(const VtOnlineItem *item) {
	if (!item || !vt_online_url_valid(item->url) ||
	    (item->kind != VT_ONLINE_RADIO && item->kind != VT_ONLINE_FEED)) return -1;
	VtOnlineItem *items = calloc(VT_ONLINE_MAX, sizeof(*items));
	if (!items) return -1;
	int count = vt_online_load_saved(items, VT_ONLINE_MAX);
	if (count < 0) { free(items); return -1; }
	int target=-1;
	for (int i = 0; i < count; i++)if (!strcmp(item->url, items[i].url))target=i;
	if(target<0){if(count==VT_ONLINE_MAX){free(items);return -1;}target=count++;}
	/* Re-starring refreshes metadata from older favorites without duplicates. */
	items[target] = *item;
	json_t *array = json_array();
	if (!array) { free(items); return -1; }
	for (int i = 0; i < count; i++) {
		json_t *entry = json_pack("{s:s,s:s,s:s,s:i,s:s,s:s,s:s,s:s}", "title", items[i].title,
		    "url", items[i].url, "id", items[i].id, "kind", items[i].kind,
		    "artwork",items[i].artwork_url,"author",items[i].author,
		    "description",items[i].description,"detail",items[i].detail);
		if (!entry || json_array_append_new(array, entry) < 0) {
			free(items); json_decref(array); return -1;
		}
	}
	free(items);
	FILE *file = fopen(SAVED_TEMP, "wb");
	int result = file ? json_dumpf(array, file, JSON_INDENT(2)) : -1;
	if (file) { if (fflush(file)) result = -1; if (fclose(file)) result = -1; }
	json_decref(array);
	if (result < 0) { sceIoRemove(SAVED_TEMP); return -1; }
	SceIoStat status;
	int old = sceIoGetstat(SAVED_PATH, &status) >= 0;
	sceIoRemove(SAVED_BACKUP);
	if (old && sceIoRename(SAVED_PATH, SAVED_BACKUP) < 0) return -1;
	if (sceIoRename(SAVED_TEMP, SAVED_PATH) < 0) {
		if (old) sceIoRename(SAVED_BACKUP, SAVED_PATH);
		return -1;
	}
	sceIoRemove(SAVED_BACKUP); return 0;
}

int vt_online_resolve_show(VtOnlineItem *item,volatile int *cancel) {
	if(!item||item->kind!=VT_ONLINE_SHOW||!item->id[0]||strlen(item->id)>24||strspn(item->id,"0123456789")!=strlen(item->id))return -1;
	char url[160];snprintf(url,sizeof(url),"https://itunes.apple.com/lookup?id=%s&entity=podcast",item->id);
	size_t size=0;char *data=fetch(url,cancel,&size);json_t *root=data?json_loadb(data,size,0,NULL):NULL;free(data);
	json_t *entry=json_array_get(json_object_get(root,"results"),0);const char *feed=json_text(entry,"feedUrl");int result=-1;
	if(vt_online_url_valid(feed)){snprintf(item->url,sizeof(item->url),"%s",feed);item->kind=VT_ONLINE_FEED;result=0;}
	json_decref(root);return result;
}
int vt_online_fetch_cover(VtOnlineItem *item, volatile int *cancel) {
	/* Favorites created before artwork support still have the catalogue UUID. */
	if(item && item->kind==VT_ONLINE_RADIO && !item->artwork_url[0] && strlen(item->id)==36 &&
	   strspn(item->id,"0123456789abcdefABCDEF-")==36) {
		char url[192];snprintf(url,sizeof(url),"https://de1.api.radio-browser.info/json/stations/byuuid/%s",item->id);
		size_t size=0;char *data=fetch(url,cancel,&size);json_t *root=data?json_loadb(data,size,0,NULL):NULL;free(data);
		json_t *entry=json_array_get(root,0);
		snprintf(item->artwork_url,sizeof(item->artwork_url),"%s",json_text(entry,"favicon"));
		snprintf(item->description,sizeof(item->description),"%s",json_text(entry,"tags"));json_decref(root);
	}
	if(!item || !vt_online_url_valid(item->artwork_url))return -1;
	unsigned hash=2166136261u;
	for(const unsigned char *p=(unsigned char*)item->artwork_url;*p;p++)hash=(hash^*p)*16777619u;
	/* Sixteen slots, one image per slot, at most 32 MiB on disk. */
	char stamp[192],path[192];snprintf(stamp,sizeof(stamp),"ux0:data/VitaMediaDeck/cover-%02u.id",hash%16);
	unsigned previous=0;FILE *f=fopen(stamp,"rb");if(f){fread(&previous,1,sizeof(previous),f);fclose(f);}
	const char *extensions[]={"jpg","png","webp","gif","bmp"};SceIoStat st;
	if(previous==hash)for(int i=0;i<5;i++) {
		snprintf(path,sizeof(path),"ux0:data/VitaMediaDeck/cover-%08x.%s",hash,extensions[i]);
		if(sceIoGetstat(path,&st)>=0&&st.st_size>0){snprintf(item->artwork_path,sizeof(item->artwork_path),"%s",path);return 0;}
	}
	size_t size=0;char *data=fetch(item->artwork_url,cancel,&size);if(!data)return -1;
	int png=size>=8&&!memcmp(data,"\x89PNG\r\n\x1a\n",8);
	int encoding=png?1:size>=3&&(unsigned char)data[0]==255&&(unsigned char)data[1]==216&&(unsigned char)data[2]==255?0:
	    size>=12&&!memcmp(data,"RIFF",4)&&!memcmp(data+8,"WEBP",4)?2:
	    size>=6&&(!memcmp(data,"GIF87a",6)||!memcmp(data,"GIF89a",6))?3:
	    size>=2&&!memcmp(data,"BM",2)?4:-1;
	if(encoding<0){free(data);return -1;}
	/* Invalidate the old slot before replacing its image, including encoding. */
	sceIoRemove(stamp);
	for(int i=0;i<5;i++){snprintf(path,sizeof(path),"ux0:data/VitaMediaDeck/cover-%08x.%s",previous,extensions[i]);sceIoRemove(path);}
	snprintf(path,sizeof(path),"ux0:data/VitaMediaDeck/cover-%08x.%s",hash,extensions[encoding]);
	f=fopen(path,"wb");int ok=0;if(f){ok=fwrite(data,1,size,f)==size;if(fflush(f))ok=0;if(fclose(f))ok=0;}free(data);
	if(!ok){sceIoRemove(path);return -1;}
	f=fopen(stamp,"wb");if(f){fwrite(&hash,1,sizeof(hash),f);fclose(f);}
	snprintf(item->artwork_path,sizeof(item->artwork_path),"%s",path);return 0;
}
