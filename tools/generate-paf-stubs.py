#!/usr/bin/env python3
"""Generate app-only weak stubs from the audited native PAF ABI facts."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--metadata',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--generator',required=True);a=p.parse_args();data=json.loads(a.metadata.read_text());a.output.mkdir(parents=True,exist_ok=True)
lines=['version: 2','firmware: 3.60','modules:','  ScePaf:','    nid: 0x00000000','    libraries:']
for lib in sorted({x['library'] for x in data['functions']}):
 funcs=[x for x in data['functions'] if x['library']==lib]
 lines += ['      '+lib+':','        kernel: false','        nid: '+funcs[0]['library_nid'],'        functions:']
 lines += ['          '+x['name']+': '+x['nid'] for x in funcs]
yml=a.output/'vmd-paf.yml';yml.write_text('\n'.join(lines)+'\n')
subprocess.run([a.generator,'-c',str(yml),str(a.output)],check=True)
