#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${MESENCE_TEST_BUILD_DIR:-"$ROOT/tools/mesencetest/build"}
mkdir -p "$OUT"
python3 - "$ROOT" "$OUT" <<'PY'
import concurrent.futures,pathlib,re,subprocess,sys,os
root,out=map(pathlib.Path,sys.argv[1:])
sources=re.findall(r'src/third_party/mesence/[^\s\\]+\.cpp',(root/'src/third_party/mesence/mesence_sources.mk').read_text())
sources+=['src/nes/mesence/mesence_bridge.cpp','tools/mesencetest/bridge_test.cpp']
flags=['-std=c++20','-O2','-ffunction-sections','-fdata-sections','-DPS2_PORT','-DMESEN_NES_ONLY','-I'+str(root/'src/third_party/mesence/src'),'-I'+str(root/'src/third_party/mesence/src/Core'),'-I'+str(root/'src/nes/mesence')]
if os.environ.get('SANITIZE')=='1': flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
latest_header=max(p.stat().st_mtime for p in (root/'src/third_party/mesence').rglob('*') if p.suffix in ('.h','.hpp','.ipp'))
import json
stamp=out/'flags.json'
unchanged=stamp.exists() and stamp.read_text()==json.dumps(flags)
if not unchanged and stamp.exists(): stamp.unlink()
def build(src):
 target=out/(src.replace('/','_')+'.o')
 if unchanged and target.exists() and target.stat().st_mtime>=max((root/src).stat().st_mtime,latest_header): return str(target)
 subprocess.run([os.environ.get('CXX','g++'),*flags,'-c',str(root/src),'-o',str(target)],check=True)
 return str(target)
with concurrent.futures.ThreadPoolExecutor(max_workers=int(os.environ.get('JOBS','4'))) as pool: objects=list(pool.map(build,sources))
for src in ['miniz.c','miniz_tdef.c','miniz_tinfl.c','miniz_zip.c']:
 target=out/(src+'.o')
 subprocess.run(['gcc','-O1','-c',str(root/'src/third_party/miniz'/src),'-o',str(target)],check=True)
 objects.append(str(target))
subprocess.run(['g++',*flags,*objects,'-Wl,--gc-sections','-pthread','-o',str(out/'bridge_test')],check=True)
subprocess.run(['g++',*flags,str(root/'tools/mesencetest/audio_equivalence_test.cpp'),
 str(root/'tools/mesencetest/OriginalHermiteResampler.cpp'),
 str(root/'src/third_party/mesence/src/Utilities/Audio/HermiteResampler.cpp'),
 '-o',str(out/'audio_equivalence_test')],check=True)
subprocess.run([str(out/'audio_equivalence_test')],check=True)
stamp.write_text(json.dumps(flags))
PY
"$OUT/bridge_test"
