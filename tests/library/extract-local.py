from pathlib import Path
import re,sys
source=Path('src/ui/local_media_screen.c').read_text()
end=source.index('} LocalMediaIndexHeader;')+len('} LocalMediaIndexHeader;')
start=source.rfind('typedef struct {',0,end)
header=source[start:end]
start=source.index('int ui_local_media_search(')
opening=source.index('{',start);level=1;at=opening+1
while level:
 if source[at]=='{':level+=1
 elif source[at]=='}':level-=1
 at+=1
limit=re.search(r'^#define LOCAL_MAX_ITEMS .+$',source,re.M).group()
Path(sys.argv[1]).write_text(limit+'\n#define LOCAL_INDEX_PATH "local.idx"\n'+header+'\n'+source[start:at]+'\n')
