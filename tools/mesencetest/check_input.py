from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'src/nes/system/nessystem_mesence.cpp').read_text()
a=s.index('static Uint8 MapPad(');b=s.index('\nstatic void AudioSink(',a)
code="""#include <cassert>
#include <cstdio>
#include "types.h"
#include "snio.h"
"""+s[a:b]+"""
int main() {
 const Uint16 keys[]={SNESIO_JOY_B,SNESIO_JOY_Y,SNESIO_JOY_SELECT,SNESIO_JOY_START,SNESIO_JOY_UP,SNESIO_JOY_DOWN,SNESIO_JOY_LEFT,SNESIO_JOY_RIGHT};
 for(unsigned b=0;b<256;++b) {
  Uint16 snes=0;for(unsigned i=0;i<8;++i)if(b&(1u<<i))snes|=keys[i];
  assert(MapPad(snes,FALSE)==b && MapPad(snes,TRUE)==b);
  assert(MapPad(snes|SNESIO_JOY_A,FALSE)==b);
  assert(MapPad(snes|SNESIO_JOY_A,TRUE)==(b|1));
  assert(MapPad(snes|SNESIO_JOY_X,FALSE)==b);
  assert(MapPad(snes|SNESIO_JOY_X,TRUE)==(b|2));
 }
 assert(MapPad(EMUSYS_DEVICE_DISCONNECTED,FALSE)==0 && MapPad(EMUSYS_DEVICE_DISCONNECTED,TRUE)==0);
 puts("NES frontend input: all combinations, turbo phases and disconnect PASS");
}
"""
with tempfile.TemporaryDirectory(prefix='nes-input-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 includes = ['src/common/base', 'src/app', 'src/snes/core', 'src/snes/apu', 'src/snes/cpu']
 subprocess.run(['g++','-O2',*['-I'+str(root/i) for i in includes],str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
