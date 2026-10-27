#!/usr/bin/env python3
"""Regression for the actual category entries with an empty local image index."""
from pathlib import Path
import argparse, subprocess, tempfile
r=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--negative-ref');args=p.parse_args()
def run(source,out,name):
 blocks=[]
 for name in ['local_images_shortcut','screen_local_count','screen_count','screen_item','screen_open_action']:
  import re
  m=re.search(r'^static [^\n]+\b'+name+r'\([^;]*?\)\s*\{',source,re.M)
  if not m:continue
  start=m.start();at=m.end();level=1
  while level:
   if source[at]=='{':level+=1
   elif source[at]=='}':level-=1
   at+=1
  blocks.append(source[start:at])
 (out/'local-images.inc').write_text('\n'.join(blocks))
 command=['clang','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-I'+str(r/'src'),'-I'+str(out)]
 if 'static int screen_open_action' in source:command+=['-DTEST_HAS_ENTRY_ACTION']
 binary=out/'entries';subprocess.run(command+[str(r/'tests/library/local-images.c'),'-o',str(binary)],check=True)
 return subprocess.run([str(binary)],check=False).returncode
with tempfile.TemporaryDirectory(prefix='vmd-local-images-') as folder:
 out=Path(folder);assert run((r/'src/ui/local_media_screen.c').read_text(),out,'current')==0
 if args.negative_ref:
  old=subprocess.check_output(['git','show',args.negative_ref+':src/ui/local_media_screen.c'],cwd=r,text=True)
  assert run(old,out,'previous')==1
  print('Previous missing-local-image-entry negative control: normal exit 1, no signal.')
