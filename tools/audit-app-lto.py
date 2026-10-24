#!/usr/bin/env python3
"""Read-only coverage audit of the CMake Makefiles release's actual LTO objects."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--readelf', type=Path,
                    default=Path(os.environ.get('VITASDK', '/missing-vitasdk')) / 'bin/arm-vita-eabi-readelf')
args = parser.parse_args()
build = args.build.resolve()

def inspect(path):
    return subprocess.check_output([str(args.readelf), '-SW', str(path)], text=True)

def require(condition, message):
    if not condition:
        raise SystemExit(message)

require((build / 'CMakeCache.txt').is_file(), 'missing CMake build directory')
require('VITAMEDIADECK_ENABLE_LTO:BOOL=ON' in (build / 'CMakeCache.txt').read_text(), 'app LTO is disabled')
targets = {
    'vitamediadeck': build,
    'vita_hw_decoder': build / 'packages/vita-hw-decoder',
    'vita_sw_decoder': build / 'packages/vita-sw-decoder',
    'vita_https': build / 'packages/vita-https',
    'vitamediadeck_iptv_source': build,
}
report = {}
for name, directory in targets.items():
    target = directory / ('CMakeFiles/' + name + '.dir')
    flags = (target / 'flags.make').read_text()
    require('-flto' in flags and '-fuse-linker-plugin' in flags, name + ': missing compile LTO flags')
    command = (target / 'link.txt').read_text()
    tokens = shlex.split(command.splitlines()[0])
    if name == 'vitamediadeck':
        objects = [directory / token for token in tokens if token.endswith(('.obj', '.o'))]
        require(objects, 'application link contains no object files')
        enabled = sum('.gnu.lto_' in inspect(path) for path in objects)
        total = len(objects)
    else:
        require('gcc-ar' in command and 'gcc-ranlib' in command, name + ': compiler-matched archive tools missing')
        archive = directory / ('lib' + name + '.a')
        members = inspect(archive).split('File: ')[1:]
        total = len(members)
        enabled = sum('.gnu.lto_' in member for member in members)
    require(total and enabled == total, name + ': not every app-owned C object contains LTO information')
    report[name] = {'objects': total, 'with_gcc_lto': enabled}
print(json.dumps({'coverage': report,
                  'scope': 'App-owned C targets only; external SDK objects, assembly and prebuilt modules are separate.',
                  'runtime_performance_measured': False}, indent=2))
