from pathlib import Path
import sys
source=Path("src/media/background_playback.c").read_text()
start=source.index("int vt_background_playback_library_sample(")
opening=source.index("{",start);level=1;at=opening+1
while level:
 if source[at]=="{":level+=1
 elif source[at]=="}":level-=1
 at+=1
Path(sys.argv[1]).write_text(source[start:at]+"\n")
