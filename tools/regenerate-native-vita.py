#!/usr/bin/env python3
"""Compile the original native resource with the pinned psp2cxml-tool.
No firmware, proprietary SDK or extracted texture is an input.
"""
import argparse,hashlib,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--compiler',type=Path,required=True);p.add_argument('--check',action='store_true');a=p.parse_args()
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='vmd-cxml-') as temporary:
    xml=Path(temporary)/'legacy_vita.xml';xml.write_bytes((r/'assets/paf/legacy_vita.xml').read_bytes())
    subprocess.run([str(a.compiler.resolve()),str(xml)],cwd=temporary,check=True)
    data=xml.with_suffix('.rco').read_bytes()
lines=['/* Generated from assets/paf/legacy_vita.xml; original resource, no Sony assets. */','static const unsigned char vmd_native_vita_resource[] __attribute__((aligned(16))) = {']
lines+=['    '+','.join('0x%02x'%v for v in data[i:i+16])+',' for i in range(0,len(data),16)];lines+=['};']
result='\n'.join(lines)+'\n';target=r/'src/ui/native_vita_resource.inc'
if a.check:
    assert target.read_text()==result,'Native resource differs; regenerate and audit it'
else:target.write_text(result)
print('Native resource:',len(data),'bytes; SHA256',hashlib.sha256(data).hexdigest())
