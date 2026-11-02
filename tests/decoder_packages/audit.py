"""Read-only proof of the four archives' compiled separation (not device QA)."""
from pathlib import Path
import argparse,os,re,subprocess
parser=argparse.ArgumentParser();parser.add_argument('--build',required=True,type=Path);args=parser.parse_args()
nm=Path(os.environ['VITASDK'])/'bin/arm-vita-eabi-gcc-nm'
report={}
for kind in ('hw-video','sw-video','hw-audio','sw-audio'):
 target='vita_'+kind.replace('-','_')+'_decoder'
 archive=args.build/'packages'/('vita-'+kind+'-decoder')/('lib'+target+'.a')
 symbols=subprocess.check_output([str(nm),'-g',str(archive)],text=True)
 if 'video' in kind:
  assert not re.search(r'(sceAudiodec|sceAudioOut|swr_|vita_(?:hw|sw)_audio_decoder_|elementary_audio_|software_audio_)',symbols),kind
  assert target+'_open' in symbols and target+'_seek' in symbols,kind
 else:
  assert not re.search(r'(vita2d_|sceVideodec|sceAudioOut|vita_(?:hw|sw)_video_decoder_)',symbols),kind
  assert target+'_create' in symbols and target+'_destroy' in symbols,kind
  if kind=='hw-audio':assert 'sceAudiodecDecode' in symbols and not re.search(r'\b(?:avcodec_|swr_)',symbols)
  else:assert 'swr_convert' in symbols and 'avcodec_receive_frame' in symbols and 'sceAudiodec' not in symbols
 report[kind]='independent archive: passed'
root=Path(__file__).resolve().parents[2]
commands=__import__('json').loads((args.build/'compile_commands.json').read_text())
assert sum(Path(c['file']).name=='audio_playback.c' for c in commands)==1
for kind in ('hw','sw'):
 assert not list((root.parent/f'vita-{kind}-video-decoder/src').rglob('*audio*.c'))
print(__import__('json').dumps({'archives':report,'shared_playback_engine_compiled':1,'physical_runtime_tested':False},indent=2))
