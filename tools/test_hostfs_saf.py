#!/usr/bin/env python3
"""Host-only mock of HostFS RPCs. Does not emulate a real SAF backend."""
from pathlib import Path
import subprocess
import tempfile
import os

root = Path(__file__).resolve().parents[1]
source = (root / "src/app/main.cpp").read_text()
start = source.index('extern "C" int MainBuildHostIOPath')
end = source.index("void MainSetBootDir", start)
prefix = '\n#include <cassert>\n#include <cstdio>\n#include <cstring>\n#include <cerrno>\n#include <vector>\n#include <string>\nstatic const char *_Main_pBootPath =\n "host:content://com.android.externalstorage.documents/tree/primary%3APS2/document/primary%3APS2%2FSNESticle-diag2.elf";\nstruct iox_dirent_t { char name[256]; };\nstatic std::vector<std::string> entries;\nstatic std::string lastUri;\nstatic int idx, encodedOpens, closed, relativeResult = -2, uriResult = 42;\nstatic int fileXioDopen(const char *path) {\n if (strncmp(path,"host:content://",15)) return relativeResult;\n lastUri=path; ++encodedOpens; idx=0; return uriResult;\n}\nstatic int fileXioDread(int fd, iox_dirent_t *e) {\n (void)fd;\n if (idx >= (int)entries.size()) return 0;\n snprintf(e->name,sizeof(e->name),"%s",entries[idx++].c_str()); return 1;\n}\nstatic int fileXioDclose(int fd) { (void)fd; ++closed; return 0; }\nstatic void DLog(const char *, ...) {}\n'
suffix = 'static void reset(std::vector<std::string> e) {\n entries=e; idx=encodedOpens=closed=0; relativeResult=-2; uriResult=42;\n}\nint main() {\n char uri[1024];\n assert(MainBuildHostIOPath("host:BGM/Track 01.xm",uri,sizeof(uri)));\n assert(strstr(uri,"primary%3APS2%2FBGM%2FTrack%2001.xm"));\n reset({"primary:PS2/BGM/Track 01.xm","primary:PS2/BGM/Track 02.mod"});\n assert(MainHostOpenDirectory("host:BGM")==42 && encodedOpens==2 && closed==1);\n reset({"primary:PS2/BGM/Track 01.xm"});\n assert(MainHostOpenDirectory("host:BGM/")==42 && lastUri.find("%2FBGM")!=std::string::npos && lastUri.back()==\'M\');\n reset({"primary:PS2/BGM","primary:PS2/SNESticle.elf"});\n assert(MainHostOpenDirectory("host:BGM")==-2 && encodedOpens==1 && closed==1);\n reset({"primary:PS2/BGM-other/Track.xm"});\n assert(MainHostOpenDirectory("host:BGM")==-2 && encodedOpens==1);\n reset({"Track.xm"});\n assert(MainHostOpenDirectory("host:BGM")==-2);\n reset({});\n assert(MainHostOpenDirectory("host:BGM")==-2);\n reset({"primary:PS2/BGM/nested/Track.xm"});\n assert(MainHostOpenDirectory("host:BGM")==-2);\n reset({"primary:PS2/BGM/Track.xm","primary:PS2/SNESticle.elf"});\n assert(MainHostOpenDirectory("host:BGM")==-2);\n reset({}); relativeResult=17;\n assert(MainHostOpenDirectory("host:BGM")==17 && encodedOpens==0);\n reset({});\n assert(MainHostOpenDirectory("host:../BGM")==-2 && encodedOpens==0);\n assert(MainHostOpenDirectory("host:BGM/%2E%2E")==-2 && encodedOpens==0);\n assert(MainHostOpenDirectory("host:")==-2 && encodedOpens==0);\n reset({}); uriResult=-2;\n assert(MainHostOpenDirectory("host:BGM")==-2 && encodedOpens==1);\n puts("13 HostFS assertions and URI encoding: PASS");\n}\n'
with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / "test.cpp"
    binary = Path(tmp) / "hostfs_test"
    src.write_text(prefix + source[start:end] + suffix)
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-Wall",
                    "-Wextra", "-Werror", str(src), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
