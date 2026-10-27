from pathlib import Path
import sys
source=Path("src/media/background_playback.c").read_text()
functions=[]
for name in ["static void background_decoder_clock_start(", "static int play_decoder_source("]:
 start=source.index(name);opening=source.index("{",start);level=1;at=opening+1
 while level:
  if source[at]=="{":level+=1
  elif source[at]=="}":level-=1
  at+=1
 functions.append(source[start:at])
Path(sys.argv[1]).write_text("\n\n".join(functions)+"\n")
