#!/usr/bin/env python3
"""Compile the actual boot-directory normalizer; check startup call-site policy.
No PS2 driver/GS/RPC simulation is implied by this host test.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/app/main.cpp').read_text()
fn = source[source.index('void MainSetBootDir('):source.index('/* Your program\'s main entry point */')]
program = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
static char _Main_BootDir[256];
static void DLog(const char *, ...) {}
''' + fn + r'''
static void check(const char *path, const char *expected) {
    MainSetBootDir(path);
    assert(strcmp(_Main_BootDir, expected) == 0);
}
int main() {
    check(nullptr, "");
    check("", "");
    check("SNESticle.elf", "");
    check("/SNESticle.elf", "");
    check("apps/SNESticle.elf", "");
    check("mass:/APPS/SNESticle.elf", "mass:/APPS/");
    check("mass0:SNESticle.elf", "mass0:");
    check("mc0:/BOOT/BOOT.ELF", "mc0:/BOOT/");
    check("cdrom0:\\APPS\\BOOT.ELF;1", "cdrom0:\\APPS\\");
    check("hdd0:/PART/BOOT.ELF", "hdd0:/PART/");
    check("mmce1:/BOOT.ELF", "mmce1:/");
    check("host:", "host:");
    check("host:apps/BOOT.ELF", "host:apps/");
    check("host:content://provider/tree/PS2/document/PS2%2FBOOT.ELF", "host:");
    char longPath[300];
    memset(longPath, 'a', sizeof(longPath));
    memcpy(longPath, "host:", 5);
    longPath[sizeof(longPath)-1] = 0;
    check(longPath, "");
    check("BOOT.ELF", "");
    puts("16 boot-directory normalization cases: PASS");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / 'test.cpp'
    exe = Path(tmp) / 'test'
    cpp.write_text(program)
    subprocess.run([os.environ.get('CXX','g++'), '-std=c++11','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

system = root / 'src/platform/ps2/system'
init = (system/'mainloop_init.cpp').read_text()
render = (system/'mainloop_render.cpp').read_text()
process = (system/'mainloop_process.cpp').read_text()
state = (system/'mainloop_state.cpp').read_text()
bgm = (system/'mainloop_bgm.cpp').read_text()
iop = (system/'mainloop_iop.cpp').read_text()
browser = (root/'src/platform/ps2/ui/uiBrowser.cpp').read_text()
assert 'Mx4sioLoadIfEnabled()' not in init
assert 'BgmUpdate();' not in render
assert process.index('MainLoopRender();') < process.index('else BgmUpdate();')
assert 'if (HostFsSupportIsEnabled()) _ScanHostBootBgm();' in bgm
assert 'if (!MmceIsLoaded()) return;' in bgm
assert 'if (!HddIsMounted()) return;' in bgm
assert 'if (s_volume <= 0 || !Aud_IsInitialized()) return;' in bgm
assert 'MmceProbeAvailableSlots()' not in state[state.index('void MainLoopStateSettingsLoad()'):state.index('Bool MainLoopStateSettingsSave()')]
assert 'int mmceSlots = MmceIsLoaded() ? MmceGetAvailableSlots() : 3;' in browser
assert '"host:"' not in iop[iop.index('Char *_MainLoop_IOPModulePaths[]'):iop.index('Bool _MainLoop_bAudioReady')]
assert '#if PS2_BOOT_RECOVERY\n    BootMark("[SAFE] SPU2 skipped");' in iop
print('11 boot/lazy storage source-policy assertions: PASS')
