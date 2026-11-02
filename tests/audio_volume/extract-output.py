from pathlib import Path
import re,sys
root=Path(__file__).resolve().parents[2];out=Path(sys.argv[1]);out.mkdir(exist_ok=True)
def function(text,name):
 match=re.search(r'^(?:static )?(?:int|void) '+name+r'\([^;]*?\)\s*\{',text,re.M|re.S)
 assert match,name
 start=match.start();pos=match.end();depth=1
 while depth:
  if text[pos]=='{':depth+=1
  elif text[pos]=='}':depth-=1
  pos+=1
 return text[start:pos]+'\n'
text=(root/'src/media/audio_playback.c').read_text()
definitions='\n'.join(line for line in text.splitlines() if line.startswith('#define ELEMENTARY_AUDIO_'))
(out/'output.inc').write_text(definitions+'\n'+function(text,'audio_output_thread')+function(text,'playback_set_volume'))
text=(root/'src/media/background_playback.c').read_text()
(out/'background-gain.inc').write_text(function(text,'set_port_volume')+function(text,'amplify_pcm16'))
