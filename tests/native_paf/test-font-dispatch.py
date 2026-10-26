#!/usr/bin/env python3
"""Isolate the exact production dispatch function; ARM checks compile the full TU."""
from pathlib import Path
import argparse, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('--negative-ref');args=parser.parse_args()
prefix=r'''
#include <stdio.h>
#include <stddef.h>
#define VMD_ENABLE_PAF_NATIVE 1
typedef void vita2d_font;
static int native=1, measured=240, available=816, captured, fallback;
int ui_native_vita_active(void){return native;}
int ui_font_text_width(vita2d_font*f,unsigned s,const char*t){(void)f;(void)s;(void)t;return measured;}
int ui_native_vita_text_span(float x,float y,unsigned s){(void)x;(void)y;(void)s;return available;}
int ui_native_vita_text(float x,float y,float w,unsigned s,unsigned c,const char*t){(void)x;(void)y;(void)s;(void)c;(void)t;captured=w;return 1;}
int mixed_layout(vita2d_font*f,int d,int x,int y,unsigned c,unsigned s,const char*t,void*h){(void)f;(void)d;(void)x;(void)y;(void)c;(void)s;(void)t;(void)h;fallback++;return 777;}
'''
suffix=r'''
int main(void){
 for(available=32;available<=924;available+=4){
  int result=ui_font_draw_text(NULL,100,128,0xffffffff,20,"Full native text");
  if(captured!=available || result!=measured){fprintf(stderr,"Font dispatch contract failed: native window %d, available %d, advance %d\n",captured,available,result);return 1;}
 }
 native=0;if(ui_font_draw_text(NULL,0,0,0,20,"Legacy")!=777 || fallback!=1)return 1;
 puts("Native font dispatch: available window independent from measured advance; legacy return contract passed.");return 0;
}
'''
def run(source, directory, name):
 first=source.index('int ui_font_draw_text(');last=source.index('int ui_font_draw_textf(',first)
 c=directory/(name+'.c');c.write_text(prefix+source[first:last]+suffix)
 binary=directory/name
 subprocess.run(['clang','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(binary)],check=True)
 return subprocess.run([str(binary)],check=False).returncode
with tempfile.TemporaryDirectory(prefix='vmd-font-dispatch-') as folder:
 d=Path(folder)
 assert run((root/'src/ui/font.c').read_text(),d,'current')==0
 if args.negative_ref:
  old=subprocess.check_output(['git','show',args.negative_ref+':src/ui/font.c'],cwd=root,text=True)
  assert run(old,d,'preceding')==1
  print('Preceding font dispatch negative control: normal exit 1, no signal.')
