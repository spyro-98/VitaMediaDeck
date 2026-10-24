from pathlib import Path
import sys
source=Path('src/ui/network_sources_screen.c').read_text()
start=source.index('int ui_network_browse_saved(')
opening=source.index('{',start);level=1;at=opening+1
while level:
 if source[at]=='{':level+=1
 elif source[at]=='}':level-=1
 at+=1
Path(sys.argv[1]).write_text(source[start:at]+'\n')
