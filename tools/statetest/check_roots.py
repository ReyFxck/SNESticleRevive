"""Compile actual root selection functions against filesystem/module fixtures."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[2]
state = (root / 'src/platform/ps2/system/mainloop_state.cpp').read_text()
menu = (root / 'src/platform/ps2/system/mainloop_menu.cpp').read_text()
def extract(source, start):
    first = source.index(start)
    end = source.index('{', first) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[first:end]
parts = ['''#include <cassert>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include "mainloop_state.h"
#include "embedded_irx.h"
static Char _RomPath[1024];
static Uint32 _MainLoop_StateRootHint;
static int usb=1, sd=0, mmce=3, hdd=1, usbEnabled=1, sdEnabled=0;
static int usbLoads, sdLoads;
int UsbBdmIsLoaded() { return usb; }
int Mx4sioIsLoaded() { return sd; }
int MassStorageIsEnabled() { return usbEnabled; }
int Mx4sioIsEnabled() { return sdEnabled; }
int UsbBdmLoadEmbeddedIrx() { ++usbLoads; return usbEnabled ? (usb=1,0) : -1; }
int Mx4sioLoadIfEnabled() { ++sdLoads; return sdEnabled ? (sd=1,0) : -1; }
int MmceSupportIsEnabled() { return mmce != 0; }
int MmceProbeAvailableSlots() { return mmce; }
int HddIsMounted() { return hdd; }
int HddSupportIsEnabled() { return hdd; }
int HddLoadEmbeddedIrx() { return hdd ? 0 : -1; }
int HddMapPath(const char *, char *out, int size) { snprintf(out,size,"pfs0:/game"); return hdd ? 1 : -1; }
''']
parts += [extract(state, 'enum MainLoopStateRootHintE')+';',
          extract(state, 'struct MainLoopStateRootT')+';',
          next(line for line in state.splitlines() if line.startswith('#define MAINLOOP_STATE_MAX_ROOTS '))]
for fn in ['void MainLoopStateSetPreferredRoot(', 'Bool MainLoopStateDeviceAvailable(',
           'static Bool _MainLoopStateEnsureMassDriver(', 'static Bool _MainLoopStateIsMassRoot(',
           'static Bool _MainLoopStateIsNumberedRoot(', 'static Bool _MainLoopStateIsMemCardRoot(',
           'static Bool _MainLoopStateIsMMCERoot(', 'static Bool _MainLoopStateGetHddRoot(',
           'static void _MainLoopStateAddRoot(', 'static Int32 _MainLoopStateBuildRoots(']:
    parts.append(extract(state, fn))
parts += [extract(menu, 'enum MainLoopStateManagerStorageE')+';',
          extract(menu, 'struct MainLoopStateManagerStorageT')+';',
          extract(menu, 'static const MainLoopStateManagerStorageT _MainLoop_StateManagerStorage[]')+';',
          extract(menu, 'static Bool _MainLoopStateManagerStorageAvailable('),
          extract(menu, 'static MainLoopStateDeviceE _MainLoopStateManagerStorageDevice(')]
parts.append('''
int main() {
 MainLoopStateRootT roots[MAINLOOP_STATE_MAX_ROOTS];
 strcpy(_RomPath,"mass0:/game.sfc");
 int count = _MainLoopStateBuildRoots(MAINLOOP_STATEDEVICE_AUTO,roots);
 assert(count==16);
 std::set<std::string> names;
 for (int i=0;i<count;i++) assert(names.insert(roots[i].Root).second);
 for (int i=0;i<MAINLOOP_STATEMANAGER_STORAGE_NUM;i++) {
  const char *path=_MainLoop_StateManagerStorage[i].pPath;
  auto device=_MainLoopStateManagerStorageDevice(i);
  if (!_MainLoopStateManagerStorageAvailable(i)) {
   assert(i>=MAINLOOP_STATEMANAGER_MC2 && i<=MAINLOOP_STATEMANAGER_MC7);
   MainLoopStateSetPreferredRoot(path);
   count=_MainLoopStateBuildRoots(device,roots);
   assert(count==2 && !strcmp(roots[0].Root,"mc0:") && !strcmp(roots[1].Root,"mc1:"));
   continue;
  }
  MainLoopStateSetPreferredRoot(path);
  count=_MainLoopStateBuildRoots(device,roots);
  char expected[16]; int bytes=(int)(strchr(path,':')-path)+1;
  if (device==MAINLOOP_STATEDEVICE_HDD) strcpy(expected,"pfs0:");
  else { memcpy(expected,path,bytes); expected[bytes]=0; }
  assert(count>0 && !strcmp(roots[0].Root,expected));
  assert(roots[0].bMemCard==(device==MAINLOOP_STATEDEVICE_MEMCARD || device==MAINLOOP_STATEDEVICE_MMCE));
 }
 assert(MAINLOOP_STATE_ROOT_HINT_MC0==4 && MAINLOOP_STATE_ROOT_HINT_HDD==8);
 // No driver may be started by root enumeration.
 assert(usbLoads==0 && sdLoads==0);
 usb=sd=0; usbEnabled=0; sdEnabled=1;
 assert(MainLoopStateDeviceAvailable(MAINLOOP_STATEDEVICE_USB));
 assert(_MainLoopStateEnsureMassDriver() && sd && !usbLoads && sdLoads==1);
 sdEnabled=0; sd=0;
 assert(!MainLoopStateDeviceAvailable(MAINLOOP_STATEDEVICE_USB));
 assert(!_MainLoopStateEnsureMassDriver());
 strcpy(_RomPath,"smb:/game.sfc"); mmce=0; hdd=0;
 MainLoopStateSetPreferredRoot("smb:");
 count=_MainLoopStateBuildRoots(MAINLOOP_STATEDEVICE_AUTO,roots);
 assert(count==MAINLOOP_MEMCARD_UNITS);
 puts("state roots: PASS (16 roots, native mc0/mc1, hidden old MC preferences, exact preferences, legacy IDs, MX4SIO-only, read-only enumeration)");
}
''')
with tempfile.TemporaryDirectory(prefix='state-roots-') as directory:
    source=Path(directory)/'fixture.cpp';binary=Path(directory)/'fixture'
    source.write_text('\n'.join(parts))
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror',
        '-I'+str(root/'src/common/base'),'-I'+str(root/'src/platform/ps2/system'),
        str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
