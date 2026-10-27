from pathlib import Path
import subprocess,re,json
import argparse,tempfile
r=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description="Compile the production QR payload handoff; no camera or firmware execution.")
parser.add_argument('--negative-ref',help='Optional Git revision expected to reject valid QR input')
args=parser.parse_args()
out=Path(tempfile.mkdtemp(prefix='vmd-qr-payload-'))
source=(r/'src/ui/qr_scanner.c').read_text()
def function(s,name):
 start=s.index('static int '+name+'('); end=s.index('\n}',start)+2
 return s[start:end]
worker=source[source.index('typedef struct QrDecodeWorker {'):source.index('} QrDecodeWorker;')+len('} QrDecodeWorker;')]
prefix='''#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <quirc.h>
#include "network/iptv_manifest.h"
#define QR_DECODE_WIDTH 480
#define QR_DECODE_HEIGHT 480
#define QR_URL_MAX 2048
typedef int SceUID;
static __attribute__((unused)) void log_printf(const char *f,...){(void)f;}
'''
checks=(r/'tests/qr/payload_checks.c').read_text()

quirc=r/'build/deps/src/quirc-1.2/lib'
sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
versions=[('current',source)]
if args.negative_ref: versions.append(('baseline',subprocess.check_output(['git','show',args.negative_ref+':src/ui/qr_scanner.c'],cwd=r,text=True)))
for label,s in versions:
 funcs=(function(s,'is_http_url')+'\n' if 'static int is_http_url(' in s else '')+function(s,'publish_qr_data')
 path=out/('qr-'+label+'.c');path.write_text(prefix+worker+'\n'+funcs+'\n'+checks)
 binary=out/('qr-'+label)
 subprocess.run(['clang','-O1','-g','-Wall','-Wextra','-fsanitize=address,undefined','-I'+str(r/'src'),'-I'+str(quirc),'-I'+sdk+'/usr/include/libxml2',str(path),str(r/'src/network/iptv_manifest.c'),'-lxml2','-o',str(binary)],check=True)
 run=subprocess.run([str(binary)],capture_output=True,text=True)
 (out/('qr-'+label+'.log')).write_text(run.stdout+run.stderr)
 print(label,run.returncode,run.stdout.strip(),run.stderr.splitlines()[:1])
 assert run.returncode==(0 if label=='current' else 1)
