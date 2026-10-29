#!/usr/bin/env python3
"""Build offline catalogs from app-owned English strings, never runtime user data.

The app never calls the translation service. Protected printf/controller tokens
are verified before publication; batches are cached for resumable generation.
"""
from __future__ import annotations
import ast, concurrent.futures, json, os, pathlib, re, time, urllib.parse, urllib.request
ROOT=pathlib.Path(__file__).resolve().parents[1]
I18N=ROOT/'src/i18n'
LANGUAGES={'es':'es','fr':'fr','de':'de','pt':'pt','ru':'ru','ja':'ja','ko':'ko','zh_s':'zh-CN','zh_t':'zh-TW'}
LITERAL=r'"(?:\\.|[^"\\])*"'
ATOM=r'(?:'+LITERAL+r'\s*|UI_PAD_[A-Z0-9_]+\s*)+'
PAIR=re.compile(r'\b(?:tr|vt_i18n_text)\s*\(\s*('+ATOM+r'),\s*('+ATOM+r')\)')
ENTRY=re.compile(r'\bVT_STR\(\s*([^,]+),\s*('+ATOM+r'),\s*('+ATOM+r')\)')
FORMAT=re.compile(r'%(?:\d+\$)?[-+ #0\']*(?:\*|\d+)?(?:\.(?:\*|\d+))?(?:hh|ll|h|l|j|z|t|L)?[diuoxXfFeEgGaAcspn%]|[\ue000-\uf8ff]|\n')
PADS=dict(zip(['CROSS','CIRCLE','SQUARE','TRIANGLE','L1','R1'],range(0xe000,0xe006)))
def literal(value):
 for name,code in PADS.items():value=value.replace('UI_PAD_'+name,json.dumps(chr(code)))
 pieces=re.findall(LITERAL,value)
 if re.sub(LITERAL,'',value).strip():raise ValueError('Unsupported C expression '+value)
 return ''.join(ast.literal_eval(p) for p in pieces)
def catalog():
 values=set()
 for path in I18N.glob('strings_*.def'):
  if path.name.startswith('strings_translation_'):continue
  source=re.sub(r'/\*.*?\*/','',path.read_text(),flags=re.S)
  for m in ENTRY.finditer(source):values.add(literal(m[3]))
 for path in (ROOT/'src').rglob('*.c'):
  if 'i18n' in path.parts:continue
  for m in PAIR.finditer(path.read_text()):values.add(literal(m[1]))
 values.update(['Playback','Library','View','Connections','Manage','Information','Local files','Network files'])
 return sorted(values)
def protect(text):
 tokens=[]
 def save(m):tokens.append(m[0]);return f'ZXQFMT{len(tokens)-1}QXZ'
 return FORMAT.sub(save,text),tokens

def restore(text,tokens):
 order=[]
 for m in re.finditer(r'ZXQFMT(\d+)QXZ',text):order.append(int(m[1]))
 if order!=list(range(len(tokens))):raise ValueError('Changed token order/count: '+text)
 for i,token in enumerate(tokens):text=text.replace(f'ZXQFMT{i}QXZ',token)
 if 'ZXQFMT' in text:raise ValueError('Unrestored token')
 return text

def request(texts,language):
 combined='\n'.join((f'ZXQSEP{i:03d}QXZ\n' if i else '')+text for i,text in enumerate(texts))
 query=urllib.parse.urlencode({'client':'gtx','sl':'en','tl':language,'dt':'t','q':combined})
 url='https://translate.googleapis.com/translate_a/single?'+query
 for attempt in range(5):
  try:
   with urllib.request.urlopen(url,timeout=30) as response:payload=json.load(response)
   result=''.join(part[0] for part in payload[0] if part[0])
   parts=re.split(r'\s*ZXQSEP\d{3}QXZ\s*',result)
   if len(parts)!=len(texts):raise ValueError('Batch delimiter changed')
   return [part.strip('\n ') for part in parts]
  except Exception:
   if attempt==4:raise
   time.sleep(min(15,1.5*(attempt+1)))
 raise AssertionError('unreachable')

def decorate(source,value):
 if not source.strip():return source
 lead=re.match(r'^\s*',source)[0];trail=re.search(r'\s*$',source)[0]
 return lead+value.strip()+trail

def translate_one(source,protected,tokens,service):
 if len(protected)>1500:
  # Long help chapters exceed URL/service limits. Keep paragraph boundaries and
  # split at spaces so neither printf arguments nor button glyphs are divided.
  result=[]
  for paragraph in re.split(r'(\n+)',source):
   if not paragraph.strip():result.append(paragraph);continue
   while len(paragraph)>1000:
    end=paragraph.rfind(' ',0,1000)
    if end<=0:raise ValueError('Unbreakable UI help paragraph')
    chunk=paragraph[:end];encoded,arguments=protect(chunk)
    result.append(decorate(chunk,translate_one(chunk,encoded,arguments,service)))
    result.append(' ');paragraph=paragraph[end+1:]
   if paragraph:
    encoded,arguments=protect(paragraph)
    result.append(decorate(paragraph,translate_one(paragraph,encoded,arguments,service)))
  return ''.join(result)
 words=re.findall(r'[A-Za-z]+',FORMAT.sub('',source))
 units={'DEC','fps','p','kbps','CPU','ES','BUS','XBR','MHz','MB','GB','ms','s','px'}
 if not words or all(word in units for word in words):return source
 try:return restore(request([protected],service)[0],tokens)
 except ValueError:
  # A service may merge adjacent opaque tokens or reorder arguments. Translate
  # only the natural-language spans in that case, retaining the original C ABI.
  result=[];offset=0
  for match in FORMAT.finditer(source):
   span=source[offset:match.start()]
   result.append(decorate(span,request([span],service)[0]) if span.strip() else span)
   result.append(match[0]);offset=match.end()
  span=source[offset:];result.append(decorate(span,request([span],service)[0]) if span.strip() else span)
  return ''.join(result)

def generate(code,service,entries,cache_root):
 cache=cache_root/(code+'.json')
 known=json.loads(cache.read_text()) if cache.exists() else {}
 missing=[s for s in entries if s not in known]
 while missing:
  batch=[];chars=0
  while missing and len(batch)<18 and (not batch or chars+len(missing[0])<2300):
   item=missing.pop(0);batch.append(item);chars+=len(item)+35
  prepared=[protect(s) for s in batch]
  try:
   if any(len(s)>1500 for s,_ in prepared):raise ValueError('Translate long help in bounded chunks')
   translated=request([s for s,t in prepared],service)
   translated=[restore(s,t) for s,(_,t) in zip(translated,prepared)]
  except Exception:
   translated=[]
   for source,(protected,tokens) in zip(batch,prepared):
    if not source:translated.append('');continue
    translated.append(translate_one(source,protected,tokens,service))
  for source,value in zip(batch,translated):
   value=decorate(source,value)
   if FORMAT.findall(source)!=FORMAT.findall(value):raise ValueError('Format changed in '+code+' '+source)
   known[source]=value
  tmp=cache.with_suffix('.tmp');tmp.write_text(json.dumps(known,ensure_ascii=False,indent=2)+'\n');tmp.replace(cache)
  print(f'{code}: {len(entries)-len(missing)}/{len(entries)}',flush=True)
 overrides=json.loads((I18N/'translation_overrides.json').read_text()).get(code,{})
 for source,value in overrides.items():
  if source in entries:known[source]=value
 lines=['/* Offline catalog: machine translation plus reviewed UI terminology; token-validated. */']
 for source in entries:known[source]=decorate(source,known[source])
 for source in entries:lines.append('VT_TRANSLATION('+json.dumps(source,ensure_ascii=False)+', '+json.dumps(known[source],ensure_ascii=False)+')')
 target=I18N/f'strings_translation_{code}.def';tmp=target.with_suffix('.tmp');tmp.write_text('\n'.join(lines)+'\n');tmp.replace(target)
 return code

def main():
 entries=catalog();cache=pathlib.Path(os.environ.get('VMD_TRANSLATION_CACHE','/private/tmp/vmd-translation-cache'));cache.mkdir(parents=True,exist_ok=True)
 if os.environ.get('VMD_TRANSLATION_DRY_RUN'):print('App-owned unique English strings:',len(entries));return
 with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
  tasks=[pool.submit(generate,code,language,entries,cache) for code,language in LANGUAGES.items()]
  for task in concurrent.futures.as_completed(tasks):print('Completed:',task.result(),flush=True)
if __name__=='__main__':main()
