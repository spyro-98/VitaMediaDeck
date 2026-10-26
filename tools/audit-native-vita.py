#!/usr/bin/env python3
"""Read-only audit of app-owned native PAF resources and ABI import facts."""
import hashlib,json,re
from pathlib import Path
r=Path(__file__).resolve().parents[1]
lock=dict(line.split('=',1) for line in (r/'release/DEPENDENCIES.lock').read_text().splitlines() if line and not line.startswith('#'))
for name,key in [('assets/paf/legacy_vita.xml','paf-resource.xml'),('src/ui/native_vita_resource.inc','paf-resource.embedded'),('release/paf-imports.json','paf-imports')]:
    assert hashlib.sha256((r/name).read_bytes()).hexdigest()==lock[key+'.sha256'],name
meta=json.loads((r/'release/paf-imports.json').read_text())
assert meta['revision']==lock['paf-abi-reference.commit']
assert len(meta['functions'])==33
assert len({v['name'] for v in meta['functions']})==33
explicit={'vmd_paf_widget_show':0xFF5BC957,'vmd_paf_widget_hide':0x4588699E}
for v in meta['functions']:
    expected=explicit.get(v['name'],int.from_bytes(hashlib.sha1((v['symbol']+'PAF').encode()).digest()[:4],'little'))
    assert expected==int(v['nid'],16),v['name']
    assert v['name'].startswith('vmd_paf_') and v['library'].startswith('ScePaf')
resource=bytes(int(v,16) for v in re.findall(r'0x([0-9a-f]{2})', (r/'src/ui/native_vita_resource.inc').read_text()))
assert len(resource)==2336 and hashlib.sha256(resource).hexdigest()=='2949e60991225abcb633fbd64f5b2afe0616b889771a49ff276bcb8c4af36d92'
# PAF styles/textures are symbolic runtime references, not packed Sony files.
xml=(r/'assets/paf/legacy_vita.xml').read_text();assert xml.count('<busyindicator ')==4
assert '<texturetable />' in xml and '<filetable />' in xml
for p in (r/'src').rglob('*.c'):
    if p.name!='font.c':assert not re.search(r'vita2d_(?:font|pgf|pvf)_draw_text',p.read_text()),str(p)
print('Native PAF audit: 33 app-local imports, fixed resource hashes, original symbolic-only resource and central text path passed; native firmware rendering is not verified.')

# Optional inspection of the real linked Vita ELF, without host ELF packages.
import sys,struct
if '--elf' in sys.argv:
    binary=Path(sys.argv[sys.argv.index('--elf')+1]).read_bytes()
    assert binary[:6]==b'\x7fELF\x01\x01','Expected little-endian ELF32'
    shoff=struct.unpack_from('<I',binary,32)[0]
    size,count,strings=struct.unpack_from('<HHH',binary,46)
    sections=[struct.unpack_from('<10I',binary,shoff+i*size) for i in range(count)]
    string_section=sections[strings];names=binary[string_section[4]:string_section[4]+string_section[5]]
    def section_name(s):return names[s[0]:names.index(b'\0',s[0])].decode()
    def file_offset(address):
        for s in sections:
            if s[1]!=8 and s[3]<=address<s[3]+s[5]:return s[4]+address-s[3]
        raise AssertionError(hex(address))
    stubs=next(s for s in sections if section_name(s)=='.sceLib.stubs');at=stubs[4];found={}
    while at<stubs[4]+stubs[5]:
        length,version,flags,nfunc=struct.unpack_from('<4H',binary,at)
        assert length>=36
        nid,ptr=struct.unpack_from('<II',binary,at+16);p=file_offset(ptr)
        lib=binary[p:binary.index(b'\0',p)].decode()
        if lib.startswith('ScePaf'):
            assert flags&8,lib+' must remain a weak import'
            p=file_offset(struct.unpack_from('<I',binary,at+28)[0])
            ids=set(struct.unpack_from('<'+str(nfunc)+'I',binary,p))
            expected=[v for v in meta['functions'] if v['library']==lib]
            assert expected and nid==int(expected[0]['library_nid'],16)
            assert ids=={int(v['nid'],16) for v in expected}
            found[lib]=nfunc
        at+=length
    assert sum(found.values())==33 and len(found)==5,found
    print('Linked Vita ELF: all 33 PAF functions in five weak firmware libraries verified:',json.dumps(found,sort_keys=True))
