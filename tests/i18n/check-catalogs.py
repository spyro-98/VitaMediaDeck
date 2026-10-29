#!/usr/bin/env python3
"""Validate generated UI catalogs without making any network requests."""
import importlib.util,json,pathlib,re
root=pathlib.Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('translations',root/'tools/generate-translations.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
keys=module.catalog();overrides=json.loads((module.I18N/'translation_overrides.json').read_text())
pattern=re.compile(r'^VT_TRANSLATION\(("(?:\\.|[^"\\])*"), ("(?:\\.|[^"\\])*")\)$')
for code in module.LANGUAGES:
 records=[]
 for line in (module.I18N/f'strings_translation_{code}.def').read_text().splitlines()[1:]:
  match=pattern.fullmatch(line);assert match,(code,line)
  records.append(tuple(json.loads(value) for value in match.groups()))
 assert [key for key,_ in records]==keys,code
 for source,value in records:
  assert value or not source,(code,source)
  assert module.FORMAT.findall(source)==module.FORMAT.findall(value),(code,source,value)
  assert not re.search(r'ZXQ(?:FMT|SEP)',value),(code,source,value)
  assert value.encode().decode()==value
 for source,value in overrides[code].items():
  if source in keys:assert dict(records)[source]==value,(code,source)
 print(f'{code}: {len(records)} UI captions, printf arguments/controller glyphs/newlines/UTF-8 and reviewed terms passed.')
print('No network requests made by catalog validation.')
