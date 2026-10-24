from pathlib import Path
import sys
source=Path("src/media/background_playback.c").read_text()
chunks=[]
for marker in ["static mpg123_ssize_t vita_mpg123_read(","static off_t vita_mpg123_seek(","static mpg123_ssize_t podcast_mpg_read(","static off_t podcast_mpg_seek(","static int play_mp3("]:
 start=source.index(marker);opening=source.index("{",start);depth=1;end=opening+1
 while depth:
  if source[end]=="{":depth+=1
  elif source[end]=="}":depth-=1
  end+=1
 chunks.append(source[start:end])
Path(sys.argv[1]).write_text("\n".join(chunks)+"\n")
