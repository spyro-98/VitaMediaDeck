#!/usr/bin/env python3
"""Compile the actual main browser loop with scripted screen/playback boundaries."""
from pathlib import Path
import argparse, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--negative-ref')
args=parser.parse_args()
def run(source,directory,name):
 first=source.index('static int browse_local(void) {')
 last=source.index('\nstatic int browse_network(',first)
 (directory/'browse-local.inc').write_text(source[first:last])
 binary=directory/name
 subprocess.run(['clang','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'src'),'-I'+str(directory),str(root/'tests/library/browse-navigation.c'),'-o',str(binary)],check=True)
 return subprocess.run([str(binary)],check=False).returncode
with tempfile.TemporaryDirectory(prefix='vmd-browse-navigation-') as folder:
 d=Path(folder)
 assert run((root/'src/main.c').read_text(),d,'current')==0
 if args.negative_ref:
  source=subprocess.check_output(['git','show',args.negative_ref+':src/main.c'],cwd=root,text=True)
  assert run(source,d,'preceding')==1
  print('Preceding category-routing negative control: normal exit 1, no signal.')
