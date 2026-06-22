ISO_TOOL ?= $(firstword $(shell command -v mkisofs 2>/dev/null) $(shell command -v genisoimage 2>/dev/null) $(shell command -v xorriso 2>/dev/null))
PS2_PACKER_SRC_DIR ?= $(PS2DEV_CACHE_DIR)/ps2-packer-src
PS2_PACKER_REPO ?= https://github.com/ps2dev/ps2-packer.git
AUTO_INSTALL ?= ask
BUILD_OUTPUT_FILE ?= $(BUILD_META_DIR)/output.txt
ROMS ?= 
BUILD_COPIED_FILE ?= $(BUILD_META_DIR)/copied.txt
BUILD_START_TEXT ?= $(BUILD_META_DIR)/start.txt
BUILD_START_EPOCH ?= $(BUILD_META_DIR)/start.epoch
BUILD_ERROR_FILE ?= $(BUILD_META_DIR)/error.list
BUILD_WARN_FILE ?= $(BUILD_META_DIR)/warn.list
BUILD_OK_FILE ?= $(BUILD_META_DIR)/ok.list
BUILD_META_DIR ?= $(OBJ_DIR)/.meta
BUILD_TOTAL ?= $(words $(OBJS))
SHOW_WARN_LOG ?= 0
COLOR ?= 1
PS2DEV_CACHE_DIR ?= $(shell if [ -n "$$XDG_CACHE_HOME" ]; then printf "%s/ps2dev" "$$XDG_CACHE_HOME"; else printf "%s/.cache/ps2dev" "$$HOME"; fi)
PS2DEV_URL_ARM ?= https://github.com/ps2dev/ps2dev/releases/download/latest/ps2dev-ubuntu-22.04-arm.tar.gz
PS2DEV_URL_DEFAULT ?= https://github.com/ps2dev/ps2dev/releases/download/latest/ps2dev-ubuntu-latest.tar.gz
PS2DEV_URL ?= auto
PS2DEV_ARCHIVE ?= $(PS2DEV_ARCHIVE_DIR)/ps2dev-latest.tar.gz
PS2DEV_ARCHIVE_DIR ?= $(PS2DEV_CACHE_DIR)
PS2DEV_LOAD_LIMIT ?= $(PS2DEV_JOBS)
PS2DEV_JOBS ?= 1
PS2DEV_ENV ?= $(PS2DEV)/env.sh
PS2DEV_REF ?= master
PS2DEV_REPO ?= https://github.com/ps2dev/ps2dev.git
PS2DEV_BUILD_DIR ?= $(HOME)/.cache/snesticle-ps2dev
JOBS ?= 1
LOAD_LIMIT ?= $(JOBS)
OUTPUT_SYNC ?= --output-sync=target
.DEFAULT_GOAL := fast

PS2DEV ?= $(shell if [ -n "$$PREFIX" ]; then printf "%s/opt/ps2dev" "$$PREFIX"; else printf "%s/.local/ps2dev" "$$HOME"; fi)
PS2SDK ?= $(PS2DEV)/ps2sdk
GSKIT ?= $(PS2DEV)/gsKit

SRC_DIR := $(CURDIR)/src
OBJ_DIR := $(CURDIR)/build
PKG_DIR := $(OBJ_DIR)/pkg
EMBED_DIR := $(OBJ_DIR)/embed
TARGET        := $(OBJ_DIR)/SNESticle.elf
TARGET_PACKED := $(OBJ_DIR)/SNESticle.packed.elf
BIN2C   ?= $(PS2SDK)/bin/bin2c

EE_CC ?= $(shell if command -v ee-gcc >/dev/null 2>&1; then echo ee-gcc; elif command -v mips64r5900el-ps2-elf-gcc >/dev/null 2>&1; then echo mips64r5900el-ps2-elf-gcc; elif [ -x "$(PS2DEV)/ee/bin/ee-gcc" ]; then echo "$(PS2DEV)/ee/bin/ee-gcc"; elif [ -x "$(PS2DEV)/ee/bin/mips64r5900el-ps2-elf-gcc" ]; then echo "$(PS2DEV)/ee/bin/mips64r5900el-ps2-elf-gcc"; fi)
EE_CXX ?= $(shell if command -v ee-g++ >/dev/null 2>&1; then echo ee-g++; elif command -v mips64r5900el-ps2-elf-g++ >/dev/null 2>&1; then echo mips64r5900el-ps2-elf-g++; elif [ -x "$(PS2DEV)/ee/bin/ee-g++" ]; then echo "$(PS2DEV)/ee/bin/ee-g++"; elif [ -x "$(PS2DEV)/ee/bin/mips64r5900el-ps2-elf-g++" ]; then echo "$(PS2DEV)/ee/bin/mips64r5900el-ps2-elf-g++"; fi)
EE_STRIP ?= $(shell if command -v ee-strip >/dev/null 2>&1; then echo ee-strip; elif command -v mips64r5900el-ps2-elf-strip >/dev/null 2>&1; then echo mips64r5900el-ps2-elf-strip; elif [ -x "$(PS2DEV)/ee/bin/ee-strip" ]; then echo "$(PS2DEV)/ee/bin/ee-strip"; elif [ -x "$(PS2DEV)/ee/bin/mips64r5900el-ps2-elf-strip" ]; then echo "$(PS2DEV)/ee/bin/mips64r5900el-ps2-elf-strip"; fi)
# ps2-packer is Pixel's LZMA self-extracting ELF packer
# (https://github.com/ps2dev/ps2-packer, shipped with ps2dev/ps2dev).
# When present the ISO embeds the packed ELF instead of the stripped
# one: typically a ~70% size reduction (1.6 MB -> 490 KB).  Override
# with PS2_PACKER=... or PACK=0 to skip the pack step entirely.
PS2_PACKER ?= $(shell if [ -x "$(PS2DEV)/bin/ps2-packer" ]; then printf "%s" "$(PS2DEV)/bin/ps2-packer"; else command -v ps2-packer 2>/dev/null; fi)
PACK       ?= 1

IRX_DIR     ?= $(PS2SDK)/iop/irx

# DEBUG_BOOT_SCREEN: when set to 1 the EE side calls init_scr() in main()
# and every "[boot] ..." trace point goes to scr_printf() (BIOS debug
# font, written direct to the GS, no IOP/SIF/host: round-trip needed).
# In that mode MainLoopInit() also skips its own GS_InitGraph() so the
# debug screen survives long enough for the user to read it. Set to 0
# for normal rendering. Override on the make line if needed.
DEBUG_BOOT_SCREEN ?= 0

# MAINLOOP_DEBUG_GS_TEST: when set to 1, MainLoopRender() paints the
# whole frame solid red as the first thing every frame. Use to confirm
# whether the GS pipeline itself is alive (red = OK, still black = GS
# broken). Override on the make line: `make MAINLOOP_DEBUG_GS_TEST=1`.
MAINLOOP_DEBUG_GS_TEST ?= 0

# Conservative flags to bridge the GCC 3.2 (2003) -> GCC 15.1 (2025)
# gap in default optimization behavior. The original iaddis source was
# written assuming the older compiler's much more conservative defaults,
# so several modern auto-optimizations break hand-rolled GIF chains,
# DMA setup and pointer arithmetic in src/platform/ps2/gs/* and
# src/snes/ppu/snppublend_gs.cpp. Documented per flag:
#   -fno-tree-vectorize:                    no auto-SIMD of 256-wide blender loops
#   -fno-aggressive-loop-optimizations:     keep loops the source actually wrote
#   -fno-tree-pre:                          no PRE hoisting loads across DMA barriers
#   -fno-tree-loop-distribute-patterns:     no substituting memcpy/memset for raw loops
#   -fno-delete-null-pointer-checks:        keep null guards even when GCC "proves" non-null
#   -fno-isolate-erroneous-paths-dereference: keep "impossible" deref paths so DMA RPC works
#   -fwrapv:                                signed overflow = wrap (defined), not UB
#   -fsigned-char:                          char is signed (PS2-era assumption)
CONSERVATIVE_FLAGS := \
	-fno-strict-aliasing \
	-fno-tree-vectorize \
	-fno-aggressive-loop-optimizations \
	-fno-tree-pre \
	-fno-tree-loop-distribute-patterns \
	-fno-delete-null-pointer-checks \
	-fno-isolate-erroneous-paths-dereference \
	-fwrapv \
	-fsigned-char

CFLAGS := -G0 -O2 -Wall $(CONSERVATIVE_FLAGS) \
	-D_EE -DPS2 -DLSB_FIRST -DALIGN_DWORD -DCODE_PLATFORM=3 \
	-DDEBUG_BOOT_SCREEN=$(DEBUG_BOOT_SCREEN) \
	-DMAINLOOP_DEBUG_GS_TEST=$(MAINLOOP_DEBUG_GS_TEST)

CXXFLAGS := -G0 -O2 -Wall $(CONSERVATIVE_FLAGS) -Wno-narrowing -Wno-overflow -fno-exceptions -fno-rtti -fpermissive \
	-D_EE -DPS2 -DLSB_FIRST -DALIGN_DWORD -DCODE_PLATFORM=3 \
	-DDEBUG_BOOT_SCREEN=$(DEBUG_BOOT_SCREEN) \
	-DMAINLOOP_DEBUG_GS_TEST=$(MAINLOOP_DEBUG_GS_TEST)

# ---- ps2_drivers feature probe ---------------------------------------
# init_usb_driver() was respelled to init_usb_driver(bool) in
# ps2_drivers v2.0 (see ps2dev/ps2_drivers).  We can't tell at build
# time which version of the header the user has installed, so probe by
# compiling a one-liner against the actually-installed ps2_usb_driver.h.
# When the (bool) form compiles, define INIT_USB_DRIVER_TAKES_BOOL so
# src/app/main.cpp can pick the right branch.
INIT_USB_TAKES_BOOL := $(shell printf '#include <stdbool.h>\n#include <ps2_usb_driver.h>\nint main(void){return init_usb_driver(true);}\n' | $(EE_CC) -I$(PS2SDK)/ports/include -I$(PS2SDK)/ee/include -x c -c -o /dev/null - >/dev/null 2>&1 && echo 1)
ifeq ($(INIT_USB_TAKES_BOOL),1)
  CFLAGS   += -DINIT_USB_DRIVER_TAKES_BOOL
  CXXFLAGS += -DINIT_USB_DRIVER_TAKES_BOOL
endif
# ----------------------------------------------------------------------

INCS := \
	-I$(EMBED_DIR) \
	-I$(CURDIR)/src \
	-I$(CURDIR)/src/app \
	-I$(CURDIR)/src/common/base \
	-I$(CURDIR)/src/common/debug \
	-I$(CURDIR)/src/common/io \
	-I$(CURDIR)/src/common/media \
	-I$(CURDIR)/src/common/render \
	-I$(CURDIR)/src/modules/mcsave \
	-I$(CURDIR)/src/modules/netplay \
	-I$(CURDIR)/src/modules/netplay/protocol \
	-I$(CURDIR)/src/modules/sjpcm \
	-I$(CURDIR)/src/platform/ps2 \
	-I$(CURDIR)/src/platform/ps2/cdvd \
	-I$(CURDIR)/src/platform/ps2/common \
	-I$(CURDIR)/src/platform/ps2/gs \
	-I$(CURDIR)/src/platform/ps2/input \
	-I$(CURDIR)/src/platform/ps2/lowlevel \
	-I$(CURDIR)/src/platform/ps2/memcard \
	-I$(CURDIR)/src/platform/ps2/system \
	-I$(CURDIR)/src/platform/ps2/ui \
	-I$(CURDIR)/src/snes/apu \
	-I$(CURDIR)/src/snes/core \
	-I$(CURDIR)/src/snes/cpu \
	-I$(CURDIR)/src/snes/ppu \
	-I$(CURDIR)/src/snes/rom \
	-I$(CURDIR)/src/snes/state \
	-I$(CURDIR)/src/nes/apu \
	-I$(CURDIR)/src/nes/core \
	-I$(CURDIR)/src/nes/cpu \
	-I$(CURDIR)/src/nes/mapper \
	-I$(CURDIR)/src/nes/state \
	-I$(CURDIR)/src/nes/system \
	-I$(CURDIR)/src/third_party/miniz \
	-I$(PS2SDK)/common/include \
	-I$(PS2SDK)/ee/include \
	-I$(PS2SDK)/ports/include \
	-I$(GSKIT)/include

LIBDIRS := \
	-L$(PS2SDK)/ee/lib \
	-L$(PS2SDK)/ports/lib \
	-L$(GSKIT)/lib

# gsKit + dmaKit must come before the SDK's libgraph, because
# gsKit pulls in DMA helpers from dmaKit and the linker resolves
# left-to-right. Linking order is also why -lkernel/-lc/-lm/-lstdc++
# is kept at the end.
#
# -lps2_drivers comes from the ps2dev modern toolchain
# (https://github.com/fjtrujy/ps2_drivers). It provides
# init_ps2_filesystem_driver() and friends, plus embedded copies
# of the IRX modules they need (iomanX, fileXio, mcman, mcserv,
# cdfs, etc.). With this in place, newlib stdio fopen/fread/fwrite
# on "mc0:/...", "cdfs:/...", "mass:/..." routes through iomanX
# instead of the legacy rom0:FILEIO RPC.
#
# The static archive already embeds the IRX data via bin2c, so it
# must come *before* the libs it depends on so the linker pulls in
# the right symbols (poweroff, fileXio, iomanX, etc.).
LIBS := \
	-lgskit -ldmakit -lgskit_toolkit \
	-lps2_drivers \
	-lpoweroff -lfileXio -lcdvd \
	-lmc -lpad -lnetman -lps2ip \
	-laudsrv \
	-lpatches \
	-lcglue \
	-ldebug -lkernel -lc -lm -lstdc++ -lgcc

SRCS := \
    src/platform/ps2/ps2sdk_stubs.c \
	src/common/media/bmpfile.cpp \
	src/platform/ps2/cdvd/cd.c \
	src/modules/cdvd/cdvd_rpc.c \
	src/common/base/console.cpp \
	src/common/base/dataio.cpp \
	src/app/emumovie.cpp \
	src/app/emurom.cpp \
	src/app/emushell.cpp \
	src/app/emusys.cpp \
	src/common/base/file.cpp \
	src/common/base/font_ui.cpp \
	src/common/base/font.cpp \
	src/platform/ps2/gs/gpfifo.c \
	src/platform/ps2/gs/gpprim.c \
	src/platform/ps2/gs/gs.c \
	src/platform/ps2/gs/gskit_backend.c \
	src/platform/ps2/gs/gslist.c \
	src/platform/ps2/lowlevel/hw.s \
	src/platform/ps2/input/input.cpp \
	src/common/base/inputdevice.cpp \
	src/platform/ps2/lowlevel/libxmtap.c \
	src/platform/ps2/lowlevel/libxpad.c \
	src/app/main.cpp \
	src/platform/ps2/system/mainloop.cpp \
	src/modules/mcsave/mcsave_ee.c \
	src/platform/ps2/memcard/memcard.cpp \
	src/common/render/memspace.cpp \
	src/common/render/mixbuffer.cpp \
	src/common/io/miniz_compat.c \
	src/third_party/miniz/miniz.c \
	src/third_party/miniz/miniz_tdef.c \
	src/third_party/miniz/miniz_tinfl.c \
	src/third_party/miniz/miniz_zip.c \
	src/modules/netplay/netplay_ee.c \
	src/modules/netplay/protocol/netclient.c \
	src/modules/netplay/protocol/netpacket.c \
	src/modules/netplay/protocol/netqueue.c \
	src/modules/netplay/protocol/netrelay.c \
	src/modules/netplay/protocol/netserver.c \
	src/modules/netplay/protocol/netsocket.c \
	src/modules/netplay/protocol/netsys_ee.c \
	src/common/base/pathext.cpp \
	src/common/base/pixelformat.cpp \
	src/common/render/poly.cpp \
	src/common/debug/prof.c \
	src/common/debug/profctr.c \
	src/common/debug/proflog.c \
	src/platform/ps2/lowlevel/ps2dma.c \
	src/common/render/rendersurface.cpp \
	src/common/render/sjpcmbuffer.cpp \
	src/modules/sjpcm/sjpcm_rpc.c \
	src/snes/cpu/sn65816.S \
	src/snes/cpu/sncpu.c \
	src/snes/cpu/sncpu_c.c \
	src/snes/cpu/sndisasm.c \
	src/snes/core/sndma.cpp \
	src/snes/core/snes.cpp \
	src/snes/core/sndsp1.cpp \
	src/snes/core/sndsp2.cpp \
	src/snes/core/snesreg.cpp \
	src/snes/core/snio.cpp \
	src/snes/core/snmask128.cpp \
	src/snes/core/snmemmap.cpp \
	src/snes/ppu/snppubg.cpp \
	src/snes/ppu/snppublend_gs.cpp \
	src/snes/ppu/snppucolor.cpp \
	src/snes/ppu/snppu.cpp \
	src/snes/ppu/snppuobj.cpp \
	src/snes/ppu/snppurender8.cpp \
	src/snes/ppu/snppurender.cpp \
	src/snes/rom/snrom.cpp \
	src/snes/apu/snspcbrr.c \
	src/snes/apu/snspc.c \
	src/snes/apu/snspc_c.c \
	src/snes/apu/snspcdisasm.c \
	src/snes/apu/snspcdsp.cpp \
	src/snes/apu/snspcio.cpp \
	src/snes/apu/snspcmix.cpp \
	src/snes/apu/snspcrom.c \
	src/snes/apu/snspctimer.cpp \
	src/snes/state/snstate.cpp \
	src/common/render/surface.cpp \
	src/common/render/texture.cpp \
	src/platform/ps2/system/titleman.c \
	src/platform/ps2/ui/uiBrowser.cpp \
	src/platform/ps2/ui/uiLog.cpp \
	src/platform/ps2/ui/uiMenu.cpp \
	src/platform/ps2/ui/uiNetwork.cpp \
	src/platform/ps2/ui/uiVideo.cpp \
	src/platform/ps2/system/version.cpp \
	src/common/render/wavfile.cpp \
	src/common/debug/dbgterm.cpp \
	src/platform/ps2/system/mainloop_state.cpp \
	src/platform/ps2/system/mainloop_iop.cpp \
	src/platform/ps2/system/boot_status.cpp \
	src/platform/ps2/system/mainloop_net.cpp \
	src/platform/ps2/system/mainloop_ui.cpp \
	src/platform/ps2/system/mainloop_install.cpp \
	src/platform/ps2/system/mainloop_menu.cpp \
	src/platform/ps2/system/mainloop_browser.cpp \
	src/platform/ps2/system/mainloop_load.cpp \
	src/platform/ps2/system/mainloop_input.cpp \
	src/platform/ps2/system/mainloop_exec.cpp \
	src/platform/ps2/system/mainloop_globals.cpp \
	src/platform/ps2/system/mainloop_init.cpp \
	src/platform/ps2/system/mainloop_render.cpp \
	src/platform/ps2/system/mainloop_process.cpp \
	src/platform/ps2/system/mainloop_menu_runtime.cpp \
	src/platform/ps2/system/global_alloc.cpp \
	src/platform/ps2/system/embedded_irx.cpp \
	src/nes/core/InfoNES.cpp \
	src/nes/cpu/K6502.cpp \
	src/nes/apu/InfoNES_pAPU.cpp \
	src/nes/mapper/InfoNES_Mapper.cpp \
	src/nes/system/InfoNES_System_PS2.cpp \
	src/nes/system/nesrom.cpp \
	src/nes/system/nessystem.cpp

OBJS := \
	$(patsubst src/%.c,$(OBJ_DIR)/%.o,$(filter %.c,$(SRCS))) \
	$(patsubst src/%.cpp,$(OBJ_DIR)/%.o,$(filter %.cpp,$(SRCS))) \
	$(patsubst src/%.s,$(OBJ_DIR)/%.o,$(filter %.s,$(SRCS))) \
	$(patsubst src/%.S,$(OBJ_DIR)/%.o,$(filter %.S,$(SRCS)))

SDK_NET_IRX := ps2dev9.irx netman.irx ps2ip-nm.irx smap.irx
SDK_COMPAT_NET_IRX := ps2ip.irx ps2ips.irx smap-ps2ip.irx
SDK_MC_IRX := mcman.irx mcserv.irx
SDK_EXTRA_IRX := ioptrap.irx poweroff.irx

# IRX modules embedded directly into the ELF via bin2c.  The custom
# IRX search paths (host:, cdrom:) used by the original iaddis code
# do not work on emulators or stripped-down PS2 setups, so every IRX
# the ELF needs is shipped inside the executable and loaded via
# SifExecModuleBuffer.  The ELF is now fully self-contained: no
# IRX file has to be placed next to it on disc / mass / mc.
#
# Audio: audsrv.irx from the PS2SDK ($(PS2SDK)/iop/irx/audsrv.irx).
# Replaces the legacy iaddis SJPCM2.IRX, whose RPC server was
# unreliable on modern IOPs / emulators.  See
# src/modules/sjpcm/sjpcm_rpc.c for the EE-side wrapper.  freesd.irx
# is shipped alongside it as a fallback SPU2 driver for setups whose
# rom0:LIBSD is absent (early Japanese models, some emulators).
#
# Memory card: sio2man + mcman + mcserv embedded and loaded by
# src/platform/ps2/system/embedded_irx.cpp::MemCardLoadEmbeddedIrx,
# the same way ps2_drivers' init_memcard_driver(true) would internally
# bin2c them but pinned here to whatever PS2SDK this tree ships.
# The iaddis-era custom MCSAVE.IRX (async memory-card writer) has
# been retired: it would hang on NetherSX2 / non-faithful IOPs in
# MCSave_Init -> SifBindRpc, and mainloop_state.cpp::MCSave_Write
# already has a synchronous newlib-stdio fallback that goes through
# mcman/mcserv via iomanX.  All save paths now use that sync route.
#
# Networking: the legacy iaddis NETPLAY.IRX has been retired entirely.
# The netplay protocol runs on the EE side
# (src/modules/netplay/protocol/*.c, mirroring
# hugorsgarcia/PS2SNESticle/SNESticle/Modules/netplay/Source/*) and
# talks directly to lwIP through PS2SDK's <sys/socket.h> shims.  The
# net stack bring-up is the modern PS2SDK netman + ps2ip flow:
#   ps2dev9.irx -> netman.irx -> smap.irx -> NetManInit() ->
#   ps2ip.irx  -> ps2ipInit()
# All four .irx files are embedded into the ELF via bin2c and loaded
# from src/platform/ps2/system/embedded_irx.cpp::NetIfLoadEmbeddedIrx.
#
# The legacy iaddis CDVD.IRX is also no longer needed: app/main.cpp's
# init_ps2_filesystem_driver() brings up the modern cdfs.irx which
# registers the cdfs: device with iomanX, and the browser / ROM
# loader reach the disc through plain newlib stdio.
EMBED_IRX_NAMES := audsrv freesd sio2man mcman mcserv padman mtapman ps2dev9 netman smap ps2ip

EMBED_HEADERS := $(patsubst %,$(EMBED_DIR)/%_irx.h,$(EMBED_IRX_NAMES))

AUDSRV_IRX_PATH  ?= $(PS2SDK)/iop/irx/audsrv.irx
FREESD_IRX_PATH  ?= $(PS2SDK)/iop/irx/freesd.irx
SIO2MAN_IRX_PATH ?= $(PS2SDK)/iop/irx/sio2man.irx
MCMAN_IRX_PATH   ?= $(PS2SDK)/iop/irx/mcman.irx
MCSERV_IRX_PATH  ?= $(PS2SDK)/iop/irx/mcserv.irx
# Modern PS2SDK pad / multitap manager. Stacks on top of the modern
# sio2man.irx loaded by the memcard bring-up; works on real PS2 *and*
# emulators, unlike the BIOS-resident rom0:XPADMAN that conflicts with
# the already-loaded modern sio2man on retail hardware.
PADMAN_IRX_PATH  ?= $(PS2SDK)/iop/irx/padman.irx
MTAPMAN_IRX_PATH ?= $(PS2SDK)/iop/irx/mtapman.irx
PS2DEV9_IRX_PATH ?= $(PS2SDK)/iop/irx/ps2dev9.irx
NETMAN_IRX_PATH  ?= $(PS2SDK)/iop/irx/netman.irx
SMAP_IRX_PATH    ?= $(PS2SDK)/iop/irx/smap.irx
PS2IP_IRX_PATH   ?= $(PS2SDK)/iop/irx/ps2ip.irx

.PHONY: all clean strip list count package package-irx check-env packed elf fix-packer fast serial turbo rebuild-fast help ensure-ps2sdk install-ps2sdk ps2sdk-env ensure-ps2dev install-ps2dev-tar ps2dev-env build-begin build-summary copy-output iso-build-image ensure-ps2-packer install-ps2-packer ensure-iso-tool install-iso-tool ensure-local-ps2-packer

all: check-env $(TARGET)

check-env: ensure-ps2dev
	@test -d "$(PS2SDK)" || (echo "ERROR: PS2SDK not found at $(PS2SDK)"; exit 1)
	@test -d "$(IRX_DIR)" || (echo "ERRO: pasta de IRX nao encontrada em $(IRX_DIR)"; exit 1)

$(OBJ_DIR):
	@mkdir -p "$(OBJ_DIR)"

$(PKG_DIR):
	@mkdir -p "$(PKG_DIR)"

$(EMBED_DIR):
	@mkdir -p "$(EMBED_DIR)"

# bin2c emits a .c file containing both the array definition and the size
# value, with internal "#ifndef __<label>__" header guards. Renaming to .h
# lets us include each generated file exactly once into embedded_irx.cpp,
# which keeps the array definitions as ordinary file-scope globals.
$(EMBED_DIR)/audsrv_irx.h: $(AUDSRV_IRX_PATH) | $(EMBED_DIR)
	$(call RUN_BIN2C,$<,$@,audsrv_irx)
$(EMBED_DIR)/freesd_irx.h: $(FREESD_IRX_PATH) | $(EMBED_DIR)
	$(call RUN_BIN2C,$<,$@,freesd_irx)
$(EMBED_DIR)/sio2man_irx.h: $(SIO2MAN_IRX_PATH) | $(EMBED_DIR)
	$(call RUN_BIN2C,$<,$@,sio2man_irx)
$(EMBED_DIR)/mcman_irx.h: $(MCMAN_IRX_PATH) | $(EMBED_DIR)
	$(call RUN_BIN2C,$<,$@,mcman_irx)
$(EMBED_DIR)/mcserv_irx.h: $(MCSERV_IRX_PATH) | $(EMBED_DIR)
	$(call RUN_BIN2C,$<,$@,mcserv_irx)
$(EMBED_DIR)/padman_irx.h: $(PADMAN_IRX_PATH) | $(EMBED_DIR)
	$(call RUN_BIN2C,$<,$@,padman_irx)
$(EMBED_DIR)/mtapman_irx.h: $(MTAPMAN_IRX_PATH) | $(EMBED_DIR)
	$(call RUN_BIN2C,$<,$@,mtapman_irx)
$(EMBED_DIR)/ps2dev9_irx.h: $(PS2DEV9_IRX_PATH) | $(EMBED_DIR)
	$(call RUN_BIN2C,$<,$@,ps2dev9_irx)
$(EMBED_DIR)/netman_irx.h: $(NETMAN_IRX_PATH) | $(EMBED_DIR)
	$(call RUN_BIN2C,$<,$@,netman_irx)
$(EMBED_DIR)/smap_irx.h: $(SMAP_IRX_PATH) | $(EMBED_DIR)
	$(call RUN_BIN2C,$<,$@,smap_irx)
$(EMBED_DIR)/ps2ip_irx.h: $(PS2IP_IRX_PATH) | $(EMBED_DIR)
	$(call RUN_BIN2C,$<,$@,ps2ip_irx)

# embedded_irx.cpp #includes the generated headers, so make sure they
# exist before that file is compiled.

define RUN_BIN2C
	@mkdir -p "$(dir $(2))" "$(OBJ_DIR)/.logs"
	@log="$(OBJ_DIR)/.logs/bin2c_$(notdir $(2)).log"; \
	name=$$(basename "$(1)"); \
	start=$$(date +%s%N); \
	if $(BIN2C) "$(1)" "$(2)" "$(3)" > "$$log" 2>&1; then rc=0; else rc=$$?; fi; \
	end=$$(date +%s%N); \
	elapsed=$$(awk "BEGIN { printf \"%.2f\", ($$end - $$start) / 1000000000 }"); \
	reset=""; green=""; red=""; \
	if [ "$(COLOR)" = "1" ]; then reset="\033[0m"; green="\033[32m"; red="\033[31m"; fi; \
	if [ "$$rc" -ne 0 ]; then \
		msg=$$(head -n1 "$$log" | sed "s/^[[:space:]]*//" | cut -c1-58); \
		[ -n "$$msg" ] || msg="bin2c failed"; \
		printf "[ BIN2C ] %-23s [ $${red}ERROR$${reset} -> %ss -> %s ]\n" "$$name" "$$elapsed" "$$msg"; \
		cat "$$log"; \
		exit "$$rc"; \
	else \
		printf "[ BIN2C ] %-23s [ $${green}OK$${reset} -> %ss ]\n" "$$name" "$$elapsed"; \
	fi
endef

define RUN_LINK
	@mkdir -p "$(dir $(1))" "$(OBJ_DIR)/.logs"
	@log="$(OBJ_DIR)/.logs/link_$(notdir $(1)).log"; \
	name=$$(basename "$(1)"); \
	start=$$(date +%s%N); \
	if $(2) > "$$log" 2>&1; then rc=0; else rc=$$?; fi; \
	end=$$(date +%s%N); \
	elapsed=$$(awk "BEGIN { printf \"%.2f\", ($$end - $$start) / 1000000000 }"); \
	reset=""; green=""; yellow=""; red=""; \
	if [ "$(COLOR)" = "1" ]; then reset="\033[0m"; green="\033[32m"; yellow="\033[33m"; red="\033[31m"; fi; \
	if [ "$$rc" -ne 0 ]; then \
		msg=$$(grep -Eim1 "error:|fatal:|undefined reference|cannot find|No such file" "$$log" | sed "s/^[[:space:]]*//" | cut -c1-58); \
		[ -n "$$msg" ] || msg="link failed"; \
		printf "[ LD    ] %-23s [ $${red}ERROR$${reset} -> %ss -> %s ]\n" "$$name" "$$elapsed" "$$msg"; \
		cat "$$log"; \
		exit "$$rc"; \
	elif grep -Eiq "warning:" "$$log"; then \
		msg=$$(grep -Eim1 "warning:" "$$log" | sed "s/^[[:space:]]*//" | cut -c1-58); \
		printf "[ LD    ] %-23s [ $${yellow}WARN$${reset} -> %ss -> %s ]\n" "$$name" "$$elapsed" "$$msg"; \
	else \
		printf "[ LD    ] %-23s [ $${green}OK$${reset} -> %ss ]\n" "$$name" "$$elapsed"; \
	fi
endef

define RUN_COMPILE
	@mkdir -p "$(dir $@)" "$(OBJ_DIR)/.logs" "$(BUILD_META_DIR)"
	@log="$(OBJ_DIR)/.logs/$(notdir $@).log"; \
	src_name=$$(basename "$(2)"); \
	start=$$(date +%s%N); \
	if $(3) > "$$log" 2>&1; then rc=0; else rc=$$?; fi; \
	end=$$(date +%s%N); \
	elapsed=$$(awk "BEGIN { printf \"%.2f\", ($$end - $$start) / 1000000000 }"); \
	done_count=$$(find "$(OBJ_DIR)" -type f -name '*.o' 2>/dev/null | wc -l | tr -d ' '); \
	total="$(BUILD_TOTAL)"; \
	if [ -z "$$total" ] || [ "$$total" = "0" ]; then total=1; fi; \
	percent=$$((done_count * 100 / total)); \
	name_width=$$((23 - $${#percent})); \
	[ "$$name_width" -lt 18 ] && name_width=18; \
	src_name=$$(printf "%s" "$$src_name" | cut -c1-$$name_width); \
	reset=""; green=""; yellow=""; red=""; \
	if [ "$(COLOR)" = "1" ]; then \
		reset="\033[0m"; green="\033[32m"; yellow="\033[33m"; red="\033[31m"; \
	fi; \
	if [ "$$rc" -ne 0 ]; then \
		msg=$$(grep -Eim1 "error:|fatal:|undefined reference|No such file" "$$log" | sed "s/^[[:space:]]*//" | cut -c1-58); \
		[ -n "$$msg" ] || msg="compiler failed"; \
		printf "%s\n" "$$src_name" >> "$(BUILD_ERROR_FILE)"; \
		printf "[ %s%% ] %-3s %-*s [ $${red}ERROR$${reset} -> %ss -> %s ]\n" "$$percent" "$(1)" "$$name_width" "$$src_name" "$$elapsed" "$$msg"; \
		cat "$$log"; \
		exit "$$rc"; \
	elif grep -Eiq "warning:" "$$log"; then \
		msg=$$(grep -Eim1 "warning:" "$$log" | sed "s|.*/||; s/^[[:space:]]*//" | cut -c1-58); \
		printf "%s\n" "$$src_name" >> "$(BUILD_WARN_FILE)"; \
		printf "[ %s%% ] %-3s %-*s [ $${yellow}WARN$${reset} -> %ss -> %s ]\n" "$$percent" "$(1)" "$$name_width" "$$src_name" "$$elapsed" "$$msg"; \
		if [ "$(SHOW_WARN_LOG)" = "1" ]; then cat "$$log"; fi; \
	else \
		printf "%s\n" "$$src_name" >> "$(BUILD_OK_FILE)"; \
		printf "[ %s%% ] %-3s %-*s [ $${green}OK$${reset} -> %ss ]\n" "$$percent" "$(1)" "$$name_width" "$$src_name" "$$elapsed"; \
	fi
endef
$(OBJ_DIR)/platform/ps2/system/embedded_irx.o: $(EMBED_HEADERS)

$(OBJ_DIR)/%.o: src/%.c | $(OBJ_DIR)
	$(call RUN_COMPILE,CC,$<,$(EE_CC) $(CFLAGS) $(DEPFLAGS) $(INCS) -c "$<" -o "$@")
$(OBJ_DIR)/%.o: src/%.cpp | $(OBJ_DIR)
	$(call RUN_COMPILE,CXX,$<,$(EE_CXX) $(CXXFLAGS) $(DEPFLAGS) $(INCS) -c "$<" -o "$@")
$(OBJ_DIR)/%.o: src/%.s | $(OBJ_DIR)
	$(call RUN_COMPILE,AS,$<,$(EE_CC) $(CFLAGS) $(DEPFLAGS) $(INCS) -c "$<" -o "$@")
$(OBJ_DIR)/%.o: src/%.S | $(OBJ_DIR)
	$(call RUN_COMPILE,AS,$<,$(EE_CC) $(CFLAGS) $(DEPFLAGS) $(INCS) -c "$<" -o "$@")
$(TARGET): $(OBJS) | $(OBJ_DIR)
	$(call RUN_LINK,$@,$(EE_CXX) -o "$@" $(OBJS) $(LIBDIRS) $(LIBS))

strip: $(TARGET)
	@echo "STRIP $<"
	@$(EE_STRIP) "$(TARGET)"

# Local ps2-packer rebuilt from source.  We compile a private copy
# under build/tools/ps2-packer/ on demand whenever the system one is
# broken (see the health check inside the $(TARGET_PACKED) recipe
# below).  Users can also invoke this directly with `make fix-packer`
# when they want to skip auto-detection.
#
# Why this exists: some ps2dev pre-built bundles ship a ps2-packer
# binary whose getopt prints "Unknown option <FFFD>" even when called
# with zero args -- the binary is otherwise the right arch (verified
# AArch64-native on a real Android proot Debian by a user) but somehow
# corrupted at build / packaging time.  Rebuilding from
# https://github.com/ps2dev/ps2-packer master produces a working
# binary on the exact same host, so the fix is to always rebuild from
# source as soon as we detect the bug.  Source clone is depth=1 so
# the download is small.
PS2_PACKER_TOOLS ?= $(OBJ_DIR)/tools
PS2_PACKER_LOCAL ?= $(PS2_PACKER_SRC_DIR)/ps2-packer
PS2_PACKER_REPO_URL ?= https://github.com/ps2dev/ps2-packer.git

$(PS2_PACKER_LOCAL):
	@set -e; \
	mkdir -p "$(PS2_PACKER_TOOLS)"; \
	host_cc=$$(command -v cc 2>/dev/null || command -v gcc 2>/dev/null || command -v clang 2>/dev/null || true); \
	if [ -z "$$host_cc" ]; then \
		echo "ERROR: no host C compiler found"; \
		echo "       install with: apt install build-essential"; \
		exit 1; \
	fi; \
	if ! command -v git >/dev/null 2>&1; then \
		echo "ERROR: git not found"; \
		exit 1; \
	fi; \
	if [ ! -d "$(PS2_PACKER_SRC_DIR)/.git" ]; then \
		echo "[ PACK ] fetching ps2-packer source"; \
		rm -rf "$(PS2_PACKER_SRC_DIR)"; \
		git clone --depth=1 "$(PS2_PACKER_REPO_URL)" "$(PS2_PACKER_SRC_DIR)" >/dev/null 2>&1; \
	fi; \
	echo "[ PACK ] building local ps2-packer"; \
	$(MAKE) -C "$(PS2_PACKER_SRC_DIR)" ps2-packer CC="$$host_cc" >/dev/null; \
	cp -f "$(PS2_PACKER_SRC_DIR)/ps2-packer" "$(PS2_PACKER_LOCAL)"; \
	chmod +x "$(PS2_PACKER_LOCAL)"; \
	rm -rf "$(PS2_PACKER_TOOLS)/stub"; \
	cp -R "$(PS2_PACKER_SRC_DIR)/stub" "$(PS2_PACKER_TOOLS)/stub"
fix-packer: $(PS2_PACKER_LOCAL)
	@echo "[fix-packer] OK -> $(PS2_PACKER_LOCAL)"
	@"$(PS2_PACKER_LOCAL)" 2>&1 | head -3 | sed 's/^/[fix-packer]   /'
	@echo "[fix-packer] use com:  make iso PS2_PACKER='$(PS2_PACKER_LOCAL)'"
	@echo "[fix-packer] (ou nao faca nada -- make iso ja detecta e usa esse)"

# Packed ELF.  ps2-packer's stub decompresses the loadable segments
# back to their original load address at boot, so the resulting
# self-extracting ELF behaves exactly like the original to the BIOS
# / OPL / wLaunchELF / emulators.  Used as a model: the wLaunchELF_ISR
# Makefile (israpps/wLaunchELF_ISR) ships UNC-BOOT.ELF as the
# uncompressed build artifact and BOOT.ELF as the packed one.
#
# Path hygiene: `tr -d '\r\t'` + `sed` strip CR/tab/leading-trailing
# whitespace that proot, msys git, or a Windows checkout might have
# injected into the path string.  `--` forces ps2-packer's getopt to
# stop treating later args as options even if the path was mangled
# before we could strip it.
#
# Broken-binary detection: a healthy ps2-packer with no args prints
# "X files specified, I need exactly 2." (or similar).  A broken one
# prints "Unknown option <garbage>" -- seen on at least one user's
# AArch64 ps2dev install where the shipped binary has corrupted
# argv handling.  When we detect this, we rebuild ps2-packer locally
# from source (see $(PS2_PACKER_LOCAL) above) and use the rebuild
# silently.  No user action required.
$(TARGET_PACKED): ensure-local-ps2-packer $(TARGET)
	@set -e; \
	mkdir -p "$(dir $@)" "$(OBJ_DIR)/.logs"; \
	log="$(OBJ_DIR)/.logs/pack_$$(basename "$@").log"; \
	name=$$(basename "$@"); \
	in_elf="$(abspath $(TARGET))"; \
	out_elf="$(abspath $@)"; \
	start=$$(date +%s%N); \
	if cd "$(PS2_PACKER_SRC_DIR)" && ./ps2-packer "$$in_elf" "$$out_elf" > "$$log" 2>&1; then rc=0; else rc=$$?; fi; \
	end=$$(date +%s%N); \
	elapsed=$$(awk "BEGIN { printf \"%.2f\", ($$end - $$start) / 1000000000 }"); \
	reset=""; green=""; red=""; cyan=""; \
	if [ "$(COLOR)" = "1" ]; then reset="\033[0m"; green="\033[32m"; red="\033[31m"; cyan="\033[36m"; fi; \
	if [ "$$rc" -ne 0 ]; then \
		msg=$$(grep -Eim1 "Unable|error|failed|cannot|No such file|Unknown option" "$$log" | sed "s/^[[:space:]]*//" | cut -c1-70); \
		[ -n "$$msg" ] || msg="pack failed"; \
		printf "$${cyan}[ PACK ]$${reset} %-24s [ $${red}ERROR$${reset} -> %ss -> %s ]\n" "$$name" "$$elapsed" "$$msg"; \
		cat "$$log"; \
		exit "$$rc"; \
	else \
		bytes=$$(wc -c <"$@" | tr -d " "); \
		printf "$${cyan}[ PACK ]$${reset} %-24s [ $${green}OK$${reset} -> %ss -> %s bytes ]\n" "$$name" "$$elapsed" "$$bytes"; \
	fi
packed: $(TARGET_PACKED)

# Standalone ELF copy target.  Mirrors the ISO `out=` flow but skips
# the ISO build entirely: useful when booting via wLaunchELF / uLE /
# PSXLink / PCSX2 directly without burning a disc.
#
# Usage:
#   make elf                    # builds build/SNESticle.elf (+ .packed.elf)
#   make elf out=<pasta>        # copies SNESticle(.packed).elf -> <pasta>/
#
# When PACK=1 (default) both the unpacked and the packed ELF are
# copied; the packed one (`SNESticle.packed.elf`, ~490 KB) is the one
# you want to ship.  When PACK=0 only the unstripped ELF is copied;
# add `ee-strip` yourself if you need it.
elf: $(TARGET) $(if $(filter 1,$(PACK)),$(TARGET_PACKED))
	@if [ -n "$(strip $(out))" ]; then \
		mkdir -p "$(out)"; \
		cp -f "$(TARGET)" "$(out)/"; \
		echo "[elf] $$(basename '$(TARGET)') -> $(out)/ ($$(wc -c <'$(TARGET)') bytes, unpacked)"; \
		if [ "$(PACK)" = "1" ] && [ -f "$(TARGET_PACKED)" ]; then \
			cp -f "$(TARGET_PACKED)" "$(out)/"; \
			echo "[elf] $$(basename '$(TARGET_PACKED)') -> $(out)/ ($$(wc -c <'$(TARGET_PACKED)') bytes, packed)"; \
		fi; \
	else \
		echo "[elf] $(TARGET) ($$(wc -c <'$(TARGET)') bytes, unpacked)"; \
		if [ "$(PACK)" = "1" ] && [ -f "$(TARGET_PACKED)" ]; then \
			echo "[elf] $(TARGET_PACKED) ($$(wc -c <'$(TARGET_PACKED)') bytes, packed)"; \
		fi; \
		echo "[elf] (passe out=<pasta> para copiar)"; \
	fi


package: check-env $(TARGET) package-irx

package-irx: | $(PKG_DIR)
	@set -e; \
	echo "PKG $(PKG_DIR)"; \
	cp "$(TARGET)" "$(PKG_DIR)/SNESticle.elf"; \
	copy_sdk() { \
		f="$$1"; found=""; \
		for cand in "$$f" "$$(printf '%s' "$$f" | tr '[:upper:]' '[:lower:]')" "$$(printf '%s' "$$f" | tr '[:lower:]' '[:upper:]')"; do \
			if [ -f "$(IRX_DIR)/$$cand" ]; then \
				cp "$(IRX_DIR)/$$cand" "$(PKG_DIR)/"; \
				echo "  + $$cand"; found=1; break; \
			fi; \
		done; \
		if [ -z "$$found" ]; then echo "  ! faltando $$f em $(IRX_DIR)"; fi; \
	}; \
	echo "== SDK network IRX =="; \
	for f in $(SDK_NET_IRX); do copy_sdk "$$f"; done; \
	echo "== SDK compat network IRX =="; \
	for f in $(SDK_COMPAT_NET_IRX); do copy_sdk "$$f"; done; \
	echo "== SDK memory card IRX =="; \
	for f in $(SDK_MC_IRX); do copy_sdk "$$f"; done; \
	echo "== SDK extra IRX =="; \
	for f in $(SDK_EXTRA_IRX); do copy_sdk "$$f"; done; \
	echo "Pronto: $(PKG_DIR)"
clean:
	@set -e; \
	reset=""; green=""; yellow=""; red=""; \
	if [ "$(COLOR)" = "1" ]; then \
		reset="\033[0m"; green="\033[32m"; yellow="\033[33m"; red="\033[31m"; \
	fi; \
	if [ -d "$(OBJ_DIR)" ] || [ -f "$(TARGET_PACKED)" ]; then \
		if rm -rf "$(OBJ_DIR)" && rm -f "$(TARGET_PACKED)"; then \
			printf "$${green}[ CLEAN ]$${reset} BUILD FOLDER DELETED SUCCESSFULLY!\n"; \
		else \
			printf "$${red}[ CLEAN ]$${reset} ERROR -> FAILED TO DELETE BUILD FOLDER!\n"; \
			exit 1; \
		fi; \
	else \
		printf "$${yellow}[ CLEAN ]$${reset} NOTHING TO CLEAN.\n"; \
	fi
list:
	@printf '%s\n' $(SRCS)

count:
	@printf 'sources: %s\n' "$(words $(SRCS))"
	@printf 'objects: %s\n' "$(words $(OBJS))"


# ---- ISO (OPL-compatible, adapted from InfinityStation) ----
#
# OPL (Open PS2 Loader) exige que:
#   1. Nome do arquivo ISO: '<GAME_ID>.<NomeBonito>.iso'
#   2. ELF dentro da ISO chamado exatamente '<GAME_ID>' (sem extensao)
#   3. SYSTEM.CNF com 'BOOT2 = cdrom0:\<GAME_ID>;1'
#
# IMPORTANTE: NAO usar -iso-level 2 ou -full-iso9660-filenames!
# O CDVDMAN do OPL assume ISO9660 level 1 estrito com buffer de
# 14 caracteres por entrada do TOC. Nomes longos no PVD estouram
# o buffer e o OPL pinta a tela branca.
#
# -J -joliet-long adiciona um Joliet SVD com nomes originais (UCS-2)
# para que o launcher mostre nomes bonitos. O OPL so le o PVD
# (sector 16), entao a coexistencia e segura.
#
# SLUS_999.99 e um ID nao alocado pela Sony, comum em homebrew.
# Override: make iso ISO_GAME_ID=SLPM_625.99
#
# Uso:
#   make iso                          # gera ISO sem ROMs
#   make iso roms=<pasta>             # gera ISO com ROMs
#   make iso roms=<pasta> out=<pasta> # gera ISO + copia pra <pasta>

ISO_GAME_ID   ?= SLUS_999.99
ISO_GAME_NAME ?= SNESticle
ISO_LABEL     ?= SNESTICLE
ISO_ROOT_DIR  ?= $(OBJ_DIR)/iso_root
ISO_OUT       ?= $(OBJ_DIR)/$(ISO_GAME_ID).$(ISO_GAME_NAME).iso
ISO_BOOT      ?= $(ISO_GAME_ID)
ISO_VMODE     ?= NTSC

# User-facing knobs (lowercase)
OUT ?= 
out ?= $(OUT)
roms ?= $(ROMS)

.PHONY: iso-check iso-root iso

iso-check:
	@set -e; \
	if command -v mkisofs >/dev/null 2>&1 || command -v genisoimage >/dev/null 2>&1 || command -v xorriso >/dev/null 2>&1; then \
		exit 0; \
	fi; \
	printf "[ SETUP ] ISO tool is missing. Install xorriso now? [y/N] "; \
	if [ "$(AUTO_INSTALL)" = "yes" ] || [ "$(AUTO_INSTALL)" = "1" ]; then ans="y"; \
	elif [ "$(AUTO_INSTALL)" = "no" ] || [ "$(AUTO_INSTALL)" = "0" ]; then ans="n"; \
	else read ans; fi; \
	case "$$ans" in \
		y|Y|yes|YES|s|S|sim|SIM) $(MAKE) --no-print-directory install-iso-tool ;; \
		*) echo "[ SETUP ] skipped ISO tool install"; exit 1 ;; \
	esac
iso-root: $(TARGET) iso-check
	@rm -rf "$(ISO_ROOT_DIR)"
	@mkdir -p "$(ISO_ROOT_DIR)"
	@# Ship a ps2-packer'd ELF inside the ISO when PACK=1 (default).
	@# The packed stub is self-extracting and behaves exactly like
	@# the original to the BIOS / OPL / wLaunchELF / NetherSX2 /
	@# PCSX2.  ps2-packer typically shrinks the loadable image from
	@# ~1.6 MB to ~490 KB, ~70% off; this comes straight from the
	@# wLaunchELF_ISR Makefile pattern (UNC-BOOT.ELF + BOOT.ELF).
	@# Fall back to a plain ee-strip'd ELF when PACK=0 or when
	@# ps2-packer is not on PATH.  Real PS2 BIOS / OPL / strict
	@# emulators (AetherSX2, ArmSX2) refuse to load ELFs with debug
	@# sections present, so the fallback path strips before copying;
	@# the host build under build/ stays unstripped for symbol info.
	@set -e; \
	use_packed=0; \
	if [ "$(PACK)" = "1" ] && command -v $(PS2_PACKER) >/dev/null 2>&1; then \
		if $(MAKE) -s $(TARGET_PACKED) && [ -s "$(TARGET_PACKED)" ]; then \
			use_packed=1; \
		else \
			echo "[ ISO-ROOT ] AVISO: ps2-packer falhou; caindo pro ELF strip simples"; \
			echo "[ ISO-ROOT ]        (rode 'make packed' isolado pra ver o erro)"; \
		fi; \
	fi; \
	if [ "$$use_packed" = "1" ]; then \
		cp "$(TARGET_PACKED)" "$(ISO_ROOT_DIR)/$(ISO_BOOT)"; \
		echo "[ ISO-ROOT ] packed $(ISO_BOOT) ($$(wc -c <"$(ISO_ROOT_DIR)/$(ISO_BOOT)") bytes)"; \
	else \
		cp "$(TARGET)" "$(ISO_ROOT_DIR)/$(ISO_BOOT)"; \
		if command -v $(EE_STRIP) >/dev/null 2>&1; then \
			$(EE_STRIP) "$(ISO_ROOT_DIR)/$(ISO_BOOT)"; \
			echo "[ ISO-ROOT ] stripped $(ISO_BOOT) ($$(wc -c <"$(ISO_ROOT_DIR)/$(ISO_BOOT)") bytes)"; \
		else \
			echo "[ ISO-ROOT ] copied $(ISO_BOOT) ($$(wc -c <"$(ISO_ROOT_DIR)/$(ISO_BOOT)") bytes, unstripped)"; \
		fi; \
	fi
	@# SYSTEM.CNF must use CRLF line endings: real PS2 BIOS and the
	@# AetherSX2 / ArmSX2 / OPL parsers reject LF-only files (silent
	@# failure: black screen). NetherSX2 accepts LF, which masked the
	@# bug. Use printf with literal \r\n.
	@printf '%s\r\n' \
		"BOOT2 = cdrom0:\\$(ISO_BOOT);1" \
		"VER = 1.00" \
		"VMODE = $(ISO_VMODE)" > "$(ISO_ROOT_DIR)/SYSTEM.CNF"
	@echo "[ ISO-ROOT ] SYSTEM.CNF:"
	@cat "$(ISO_ROOT_DIR)/SYSTEM.CNF"
	@# No loose IRX files are copied into the ISO any more.  The ELF
	@# embeds every IRX it needs (audsrv, freesd, sio2man, mcman,
	@# mcserv, ps2dev9, netman, smap, ps2ip) via bin2c and loads them
	@# from memory through SifExecModuleBuffer in
	@# src/platform/ps2/system/embedded_irx.cpp.  The iaddis-era
	@# CDVD/SJPCM2/MCSAVE/NETPLAY IRXs have all been retired.
	@if [ -n "$(strip $(roms))" ]; then \
		if [ ! -d "$(roms)" ]; then \
			echo "ERRO: pasta de ROMs nao existe: $(roms)"; \
			exit 1; \
		fi; \
		mkdir -p "$(ISO_ROOT_DIR)/ROMS"; \
		( cd "$(roms)" && \
		  find . -type f \
			\( -iname '*.smc' -o -iname '*.sfc' -o -iname '*.swc' \
			   -o -iname '*.fig' -o -iname '*.nes' -o -iname '*.fds' \
			   -o -iname 'disksys.rom' -o -iname '*.zip' -o -iname '*.gz' \) \
			-exec cp -f --parents {} "$(ISO_ROOT_DIR)/ROMS/" \; ) ; \
		echo "[ ISO-ROOT ] ROMs copied from $(roms)"; \
	else \
		echo "[ ISO-ROOT ] Sem ROMs (use roms=<pasta> para incluir)"; \
	fi
	@if [ -d "$(CURDIR)/cdroot" ]; then \
		cp -a "$(CURDIR)/cdroot/." "$(ISO_ROOT_DIR)/"; \
		echo "[ ISO-ROOT ] cdroot extras copiados"; \
	fi

# Probe order is mkisofs -> genisoimage -> xorriso. Real mkisofs and
# genisoimage emit a stricter ISO9660 level-1 PVD than xorriso's
# mkisofs emulation, which is what OPL's CDVDMAN expects (it walks
# the path table assuming a 14-char filename buffer; xorriso
# sometimes leaks long names from the Joliet -joliet-long extension
# into the PVD path table and overflows that buffer -> blank screen).
# xorriso stays as a fallback so the build still works on systems
# that only ship libisoburn.
#
# Common flags across all three:
#   -iso-level 1   strict 8.3 names in the PVD (OPL requirement).
#                  Joliet (-J) provides long names in the SVD only.
#   -pad           pad the image to a multiple of 16 sectors. Real
#                  PS2 hardware and AetherSX2 both validate the
#                  trailing sector count; without -pad the disc may
#                  be reported as size 0 by libcdvd.
#   -sysid/-A/-publisher PLAYSTATION   matches the Sony master disc
#                  PVD layout. AetherSX2's CDVD detector keys on
#                  these strings to flag the image as a PS2 game.
iso:
	+@$(MAKE) --no-print-directory build-begin
	+@$(MAKE) --no-print-directory check-env
	+@$(MAKE) --no-print-directory iso-check
	+@$(MAKE) --no-print-directory iso-root
	+@$(MAKE) --no-print-directory iso-build-image
	+@$(MAKE) --no-print-directory build-summary

iso-build-image:
	@set -e; \
	mkdir -p "$$(dirname "$(ISO_OUT)")" "$(OBJ_DIR)/.logs"; \
	log="$(OBJ_DIR)/.logs/iso_$$(basename "$(ISO_OUT)").log"; \
	name=$$(basename "$(ISO_OUT)"); \
	start=$$(date +%s%N); \
	tool=""; \
	if command -v mkisofs >/dev/null 2>&1; then tool="mkisofs"; \
	elif command -v genisoimage >/dev/null 2>&1; then tool="genisoimage"; \
	elif command -v xorriso >/dev/null 2>&1; then tool="xorriso"; \
	else echo "ERROR: no ISO generator found"; exit 1; fi; \
	if [ "$$tool" = "xorriso" ]; then \
		xorriso -as mkisofs \
			-iso-level 1 -pad \
			-V "$(ISO_LABEL)" \
			-sysid PLAYSTATION \
			-A PLAYSTATION \
			-publisher PLAYSTATION \
			-J -joliet-long \
			-o "$(ISO_OUT)" \
			"$(ISO_ROOT_DIR)" > "$$log" 2>&1; \
	else \
		"$$tool" \
			-iso-level 1 -pad \
			-V "$(ISO_LABEL)" \
			-sysid PLAYSTATION \
			-A PLAYSTATION \
			-publisher PLAYSTATION \
			-J -joliet-long \
			-o "$(ISO_OUT)" \
			"$(ISO_ROOT_DIR)" > "$$log" 2>&1; \
	fi; \
	rc=$$?; \
	end=$$(date +%s%N); \
	elapsed=$$(awk "BEGIN { printf \"%.2f\", ($$end - $$start) / 1000000000 }"); \
	reset=""; green=""; red=""; cyan=""; \
	if [ "$(COLOR)" = "1" ]; then reset="\033[0m"; green="\033[32m"; red="\033[31m"; cyan="\033[36m"; fi; \
	if [ "$$rc" -ne 0 ]; then \
		msg=$$(grep -Eim1 "error:|fatal:|failed|cannot|No such file" "$$log" | sed "s/^[[:space:]]*//" | cut -c1-70); \
		[ -n "$$msg" ] || msg="ISO generation failed"; \
		printf "$${cyan}[ ISO ]$${reset} %-24s [ $${red}ERROR$${reset} -> %ss -> %s ]\n" "$$name" "$$elapsed" "$$msg"; \
		tail -60 "$$log"; \
		exit "$$rc"; \
	fi; \
	bytes=$$(wc -c <"$(ISO_OUT)" | tr -d " "); \
	printf "$${cyan}[ ISO ]$${reset} %-24s [ $${green}OK$${reset} -> %ss -> %s bytes ]\n" "$$name" "$$elapsed" "$$bytes"; \
	printf "%s\n" "$(ISO_OUT)" > "$(BUILD_OUTPUT_FILE)"; \
	if [ -n "$(strip $(out))" ]; then \
		mkdir -p "$(out)"; \
		cp -f "$(ISO_OUT)" "$(out)/"; \
		cp -f "$(TARGET)" "$(out)/SNESticle.elf"; \
		copied="$(out)/$$(basename "$(ISO_OUT)"); $(out)/SNESticle.elf"; \
		printf "$${green}[ COPY ]$${reset} ISO -> $(out)/$$(basename "$(ISO_OUT)")\n"; \
		printf "$${green}[ COPY ]$${reset} ELF -> $(out)/SNESticle.elf\n"; \
		if [ "$(PACK)" = "1" ] && [ -f "$(TARGET_PACKED)" ]; then \
			cp -f "$(TARGET_PACKED)" "$(out)/SNESticle.packed.elf"; \
			copied="$$copied; $(out)/SNESticle.packed.elf"; \
			printf "$${green}[ COPY ]$${reset} PACKED ELF -> $(out)/SNESticle.packed.elf\n"; \
		fi; \
		printf "%s\n" "$$copied" > "$(BUILD_COPIED_FILE)"; \
	fi

# ---- /ISO ----

# GCC 15.2 -O2 corrompe asm 128-bit do _MixChannel (audio direito quebrado).
# Fix do hugorsgarcia/PS2SNESticle (PORTING.md Bug 7).
$(OBJ_DIR)/snes/apu/snspcmix.o: src/snes/apu/snspcmix.cpp | $(OBJ_DIR)
	$(call RUN_COMPILE,CXX,$<,$(EE_CXX) $(CXXFLAGS) $(DEPFLAGS) -O1 $(INCS) -c "$<" -o "$@")
fast:
	@reset=""; cyan=""; if [ "$(COLOR)" = "1" ]; then reset="\033[0m"; cyan="\033[36m"; fi; printf "$${cyan}[ FAST ]$${reset} build: JOBS=$(JOBS), LOAD_LIMIT=$(LOAD_LIMIT)\n"
	+@$(MAKE) --no-print-directory build-begin
	+@$(MAKE) --no-print-directory check-env
	+@$(MAKE) --no-print-directory $(OUTPUT_SYNC) -j$(JOBS) -l$(LOAD_LIMIT) $(TARGET)
	+@$(MAKE) --no-print-directory copy-output
	+@$(MAKE) --no-print-directory build-summary
serial:
	@reset=""; green=""; if [ "$(COLOR)" = "1" ]; then reset="\033[0m"; green="\033[32m"; fi; printf "$${green}[ SERIAL ]$${reset} single job build\n"
	+@$(MAKE) --no-print-directory -j1 all

turbo:
	@reset=""; yellow=""; if [ "$(COLOR)" = "1" ]; then reset="\033[0m"; yellow="\033[33m"; fi; printf "$${yellow}[ TURBO ]$${reset} aggressive build: JOBS=4, LOAD_LIMIT=4\n"
	+@$(MAKE) --no-print-directory check-env
	+@$(MAKE) --no-print-directory $(OUTPUT_SYNC) -j4 -l4 $(TARGET)

rebuild-fast:
	+@$(MAKE) --no-print-directory clean
	+@$(MAKE) --no-print-directory check-env
	+@$(MAKE) --no-print-directory $(OUTPUT_SYNC) -j$(JOBS) -l$(LOAD_LIMIT) $(TARGET)

help:
	@reset=""; cyan=""; green=""; yellow=""; \
	if [ "$(COLOR)" = "1" ]; then \
		reset="\033[0m"; cyan="\033[36m"; green="\033[32m"; yellow="\033[33m"; \
	fi; \
	printf "$${cyan}[ HELP ]$${reset} SNESticleRevive Makefile\n"; \
	printf "\n"; \
	printf "$${green}Build commands:$${reset}\n"; \
	printf "  make                         Build ELF, default JOBS=1\n"; \
	printf "  make JOBS=2                  Build ELF with 2 workers\n"; \
	printf "  make JOBS=3                  Build ELF with 3 workers\n"; \
	printf "  make serial                  Build with one worker\n"; \
	printf "  make turbo                   Build with 4 workers\n"; \
	printf "  make rebuild-fast            Clean and rebuild\n"; \
	printf "  make clean                   Delete build folder\n"; \
	printf "\n"; \
	printf "$${green}Output commands:$${reset}\n"; \
	printf "  make OUT=/sdcard             Build and copy SNESticle.elf to OUT\n"; \
	printf "  make out=/sdcard             Same as OUT=/sdcard\n"; \
	printf "  make elf OUT=/sdcard         Build ELF/packed ELF and copy to OUT\n"; \
	printf "  make iso ROMS=/path OUT=/out Build ISO with ROM folder and copy to OUT\n"; \
	printf "  make iso roms=/path out=/out Same as uppercase variables\n"; \
	printf "  make iso PACK=0              Build ISO using unpacked ELF\n"; \
	printf "\n"; \
	printf "$${green}Info commands:$${reset}\n"; \
	printf "  make list                    List source files used by build\n"; \
	printf "  make count                   Count source files\n"; \
	printf "  make check-env               Check PS2DEV/PS2SDK environment\n"; \
	printf "\n"; \
	printf "$${green}PS2DEV commands:$${reset}\n"; \
	printf "  make install-ps2dev-tar      Download/extract prebuilt PS2DEV\n"; \
	printf "  make ps2dev-env              Write PS2DEV env file\n"; \
	printf "\n"; \
	printf "$${green}Useful variables:$${reset}\n"; \
	printf "  JOBS=1                       Number of build workers\n"; \
	printf "  LOAD_LIMIT=$(LOAD_LIMIT)                 Load limit for parallel builds\n"; \
	printf "  COLOR=0                      Disable colored output\n"; \
	printf "  SHOW_WARN_LOG=1              Print full warning logs\n"; \
	printf "  OUT=/path                    Copy final ELF to this folder\n"; \
	printf "  out=/path                    Same as OUT=/path\n"; \
	printf "  ROMS=/path                   ROM folder for ISO build\n"; \
	printf "  roms=/path                   Same as ROMS=/path\n"; \
	printf "  PACK=1                       Use packed ELF when possible\n"; \
	printf "  PS2DEV=/path                 Override PS2DEV install path\n"; \
	printf "  AUTO_INSTALL=ask             Ask before installing missing tools\n"; \
	printf "  AUTO_INSTALL=yes             Install missing tools without asking\n"; \
	printf "  AUTO_INSTALL=no              Never install missing tools\n"; \
	printf "\n"; \
	printf "$${yellow}Examples:$${reset}\n"; \
	printf "  make\n"; \
	printf "  make JOBS=2\n"; \
	printf "  make OUT=/sdcard\n"; \
	printf "  make iso ROMS=/sdcard/roms_snes OUT=/sdcard/ps2\n"

ensure-ps2dev:
	@set -e; \
	if [ -d "$(PS2SDK)" ] && { [ -x "$(PS2DEV)/ee/bin/ee-gcc" ] || [ -x "$(PS2DEV)/ee/bin/mips64r5900el-ps2-elf-gcc" ] || command -v ee-gcc >/dev/null 2>&1 || command -v mips64r5900el-ps2-elf-gcc >/dev/null 2>&1; }; then \
		$(MAKE) --no-print-directory ps2dev-env >/dev/null; \
	else \
		printf "[ SETUP ] PS2DEV/PS2SDK is missing. Install now? [y/N] "; \
		if [ "$(AUTO_INSTALL)" = "yes" ] || [ "$(AUTO_INSTALL)" = "1" ]; then ans="y"; \
		elif [ "$(AUTO_INSTALL)" = "no" ] || [ "$(AUTO_INSTALL)" = "0" ]; then ans="n"; \
		else read ans; fi; \
		case "$$ans" in \
			y|Y|yes|YES|s|S|sim|SIM) $(MAKE) --no-print-directory install-ps2dev-tar ;; \
			*) echo "[ SETUP ] skipped PS2DEV install"; exit 1 ;; \
		esac; \
	fi
install-ps2dev-tar:
	@set -e; \
	url="$(PS2DEV_URL)"; \
	arch=$$(uname -m); \
	if [ "$$url" = "auto" ]; then \
		case "$$arch" in \
			aarch64|arm64|armv7*|armv8*) url="$(PS2DEV_URL_ARM)" ;; \
			*) url="$(PS2DEV_URL_DEFAULT)" ;; \
		esac; \
	fi; \
	echo "[ps2dev] arch=$$arch"; \
	echo "[ps2dev] url=$$url"; \
	echo "[ps2dev] prefix=$(PS2DEV)"; \
	echo "[ps2dev] cache=$(PS2DEV_ARCHIVE_DIR)"; \
	if ! command -v wget >/dev/null 2>&1; then \
		echo "[ps2dev] wget not found"; \
		exit 1; \
	fi; \
	if ! command -v tar >/dev/null 2>&1; then \
		echo "[ps2dev] tar not found"; \
		exit 1; \
	fi; \
	mkdir -p "$(PS2DEV)" "$(PS2DEV_ARCHIVE_DIR)"; \
	if [ -f "$(PS2DEV_ARCHIVE)" ]; then \
		if tar -tzf "$(PS2DEV_ARCHIVE)" >/dev/null 2>&1; then \
			echo "[ps2dev] using cached archive"; \
		else \
			echo "[ps2dev] cached archive is broken"; \
			rm -f "$(PS2DEV_ARCHIVE)"; \
		fi; \
	fi; \
	if [ ! -f "$(PS2DEV_ARCHIVE)" ]; then \
		echo "[ps2dev] downloading"; \
		wget -c -O "$(PS2DEV_ARCHIVE).tmp" "$$url"; \
		tar -tzf "$(PS2DEV_ARCHIVE).tmp" >/dev/null; \
		mv -f "$(PS2DEV_ARCHIVE).tmp" "$(PS2DEV_ARCHIVE)"; \
	fi; \
	echo "[ps2dev] extracting"; \
	tar -xzf "$(PS2DEV_ARCHIVE)" --strip-components=1 -C "$(PS2DEV)"; \
	$(MAKE) --no-print-directory ps2dev-env >/dev/null; \
	if [ -d "$(PS2SDK)" ] && { [ -x "$(PS2DEV)/ee/bin/ee-gcc" ] || [ -x "$(PS2DEV)/ee/bin/mips64r5900el-ps2-elf-gcc" ]; }; then \
		echo "[ps2dev] done"; \
	else \
		echo "[ps2dev] install failed"; \
		exit 1; \
	fi

ps2dev-env:
	@mkdir -p "$(PS2DEV)"
	@printf '%s\n' \
	'export PS2DEV="$(PS2DEV)"' \
	'export PS2SDK="$(PS2SDK)"' \
	'export GSKIT="$(GSKIT)"' \
	'export PATH="$$PS2DEV/bin:$$PS2DEV/ee/bin:$$PS2DEV/iop/bin:$$PS2DEV/dvp/bin:$$PS2SDK/bin:$$PATH"' \
	> "$(PS2DEV_ENV)"
	@echo "[ps2dev] env file: $(PS2DEV_ENV)"

build-begin:
	@mkdir -p "$(BUILD_META_DIR)"
	@: > "$(BUILD_OK_FILE)"
	@: > "$(BUILD_WARN_FILE)"
	@: > "$(BUILD_ERROR_FILE)"
	@rm -f "$(BUILD_COPIED_FILE)" "$(BUILD_OUTPUT_FILE)"
	@date +%s > "$(BUILD_START_EPOCH)"
	@date "+%Y-%m-%d %H:%M:%S" > "$(BUILD_START_TEXT)"

copy-output:
	@if [ -n "$(strip $(out))" ]; then \
		mkdir -p "$(out)"; \
		cp -f "$(TARGET)" "$(out)/SNESticle.elf"; \
		bytes=$$(wc -c <"$(TARGET)"); \
		reset=""; green=""; \
		if [ "$(COLOR)" = "1" ]; then reset="\033[0m"; green="\033[32m"; fi; \
		printf "$${green}[ COPY ]$${reset} SNESticle.elf -> $(out)/ (%s bytes)\n" "$$bytes"; \
		mkdir -p "$(BUILD_META_DIR)"; \
		printf "%s\n" "$(out)/SNESticle.elf" > "$(BUILD_COPIED_FILE)"; \
	fi

build-summary:
	@set -e; \
	mkdir -p "$(BUILD_META_DIR)"; \
	end_epoch=$$(date +%s); \
	end_text=$$(date "+%Y-%m-%d %H:%M:%S"); \
	start_epoch=$$(cat "$(BUILD_START_EPOCH)" 2>/dev/null || echo "$$end_epoch"); \
	start_text=$$(cat "$(BUILD_START_TEXT)" 2>/dev/null || echo "unknown"); \
	seconds=$$((end_epoch - start_epoch)); \
	mins=$$((seconds / 60)); \
	secs=$$((seconds % 60)); \
	ok=$$(wc -l < "$(BUILD_OK_FILE)" 2>/dev/null || echo 0); \
	warn=$$(wc -l < "$(BUILD_WARN_FILE)" 2>/dev/null || echo 0); \
	err=$$(wc -l < "$(BUILD_ERROR_FILE)" 2>/dev/null || echo 0); \
	total=$$((ok + warn + err)); \
	copied=$$(cat "$(BUILD_COPIED_FILE)" 2>/dev/null || true); \
	[ -n "$$copied" ] || copied="no"; \
	output=$$(cat "$(BUILD_OUTPUT_FILE)" 2>/dev/null || true); \
	[ -n "$$output" ] || output="$(TARGET)"; \
	reset=""; cyan=""; green=""; yellow=""; red=""; \
	if [ "$(COLOR)" = "1" ]; then \
		reset="\033[0m"; cyan="\033[36m"; green="\033[32m"; yellow="\033[33m"; red="\033[31m"; \
	fi; \
	printf "\n$${cyan}[ SUMMARY ]$${reset}\n"; \
	printf "Started : %s\n" "$$start_text"; \
	printf "Finished: %s\n" "$$end_text"; \
	printf "Duration: %02dm %02ds\n" "$$mins" "$$secs"; \
	printf "Files   : %s compiled\n" "$$total"; \
	printf "OK      : $${green}%s$${reset}\n" "$$ok"; \
	printf "WARN    : $${yellow}%s$${reset}\n" "$$warn"; \
	printf "ERROR   : $${red}%s$${reset}\n" "$$err"; \
	printf "Jobs    : %s\n" "$(JOBS)"; \
	printf "Output  : %s\n" "$$output"; \
	printf "Copied  : %s\n" "$$copied"



ensure-iso-tool:
	@set -e; \
	if command -v mkisofs >/dev/null 2>&1 || command -v genisoimage >/dev/null 2>&1 || command -v xorriso >/dev/null 2>&1; then \
		exit 0; \
	fi; \
	printf "[ SETUP ] ISO tool is missing. Install xorriso now? [y/N] "; \
	if [ "$(AUTO_INSTALL)" = "yes" ] || [ "$(AUTO_INSTALL)" = "1" ]; then ans="y"; \
	elif [ "$(AUTO_INSTALL)" = "no" ] || [ "$(AUTO_INSTALL)" = "0" ]; then ans="n"; \
	else read ans; fi; \
	case "$$ans" in \
		y|Y|yes|YES|s|S|sim|SIM) $(MAKE) --no-print-directory install-iso-tool ;; \
		*) echo "[ SETUP ] skipped ISO tool install"; exit 1 ;; \
	esac
install-iso-tool:
	@set -e; \
	echo "[ SETUP ] installing xorriso"; \
	if command -v apt-get >/dev/null 2>&1; then \
		apt-get update; \
		apt-get install -y xorriso; \
	elif command -v pkg >/dev/null 2>&1; then \
		pkg install -y xorriso; \
	elif command -v pacman >/dev/null 2>&1; then \
		pacman -S --needed --noconfirm xorriso; \
	else \
		echo "[ SETUP ] no supported package manager found"; \
		exit 1; \
	fi
ensure-ps2-packer:
	@set -e; \
	if [ -n "$(PS2_PACKER)" ] && [ -x "$(PS2_PACKER)" ]; then \
		exit 0; \
	fi; \
	printf "[ SETUP ] ps2-packer is missing. Install now? [y/N] "; \
	if [ "$(AUTO_INSTALL)" = "yes" ] || [ "$(AUTO_INSTALL)" = "1" ]; then ans="y"; \
	elif [ "$(AUTO_INSTALL)" = "no" ] || [ "$(AUTO_INSTALL)" = "0" ]; then ans="n"; \
	else read ans; fi; \
	case "$$ans" in \
		y|Y|yes|YES|s|S|sim|SIM) $(MAKE) --no-print-directory install-ps2-packer ;; \
		*) echo "[ SETUP ] skipped ps2-packer install"; exit 1 ;; \
	esac

install-ps2-packer:
	@set -e; \
	echo "[ SETUP ] installing ps2-packer"; \
	if command -v pacman >/dev/null 2>&1; then \
		pacman -S --needed --noconfirm ps2-packer; \
	else \
		if command -v apt-get >/dev/null 2>&1; then \
			apt-get update; \
			apt-get install -y git make gcc g++ zlib1g-dev liblzma-dev liblzo2-dev liblz4-dev; \
		elif command -v pkg >/dev/null 2>&1; then \
			pkg install -y git make clang zlib xz lz4; \
		fi; \
		test -d "$(PS2SDK)" || (echo "[ SETUP ] PS2SDK missing. Run: make install-ps2dev-tar"; exit 1); \
		mkdir -p "$(PS2DEV_CACHE_DIR)"; \
		if [ ! -d "$(PS2_PACKER_SRC_DIR)/.git" ]; then \
			rm -rf "$(PS2_PACKER_SRC_DIR)"; \
			git clone --depth=1 "$(PS2_PACKER_REPO)" "$(PS2_PACKER_SRC_DIR)"; \
		else \
			git -C "$(PS2_PACKER_SRC_DIR)" pull --ff-only || true; \
		fi; \
		$(MAKE) -C "$(PS2_PACKER_SRC_DIR)" clean >/dev/null 2>&1 || true; \
		$(MAKE) -C "$(PS2_PACKER_SRC_DIR)" install PREFIX="$(PS2DEV)" PS2DEV="$(PS2DEV)" PS2SDK="$(PS2SDK)"; \
	fi; \
	if [ -x "$(PS2DEV)/bin/ps2-packer" ] || command -v ps2-packer >/dev/null 2>&1; then \
		echo "[ SETUP ] ps2-packer installed"; \
	else \
		echo "[ SETUP ] ps2-packer install failed"; \
		exit 1; \
	fi

ensure-local-ps2-packer:
	@set -e; \
	if [ -x "$(PS2_PACKER_LOCAL)" ] && [ -f "$(PS2_PACKER_SRC_DIR)/stub/lzma-1d00-stub" ]; then \
		exit 0; \
	fi; \
	printf "[ SETUP ] ps2-packer/stubs are missing. Build now? [y/N] "; \
	if [ "$(AUTO_INSTALL)" = "yes" ] || [ "$(AUTO_INSTALL)" = "1" ]; then ans="y"; \
	elif [ "$(AUTO_INSTALL)" = "no" ] || [ "$(AUTO_INSTALL)" = "0" ]; then ans="n"; \
	else read ans; fi; \
	case "$$ans" in \
		y|Y|yes|YES|s|S|sim|SIM) ;; \
		*) echo "[ SETUP ] skipped ps2-packer build"; exit 1 ;; \
	esac; \
	host_cc=$$(command -v cc 2>/dev/null || command -v gcc 2>/dev/null || command -v clang 2>/dev/null || true); \
	if [ -z "$$host_cc" ]; then \
		echo "ERROR: no host C compiler found"; \
		echo "Install with: apt install build-essential"; \
		exit 1; \
	fi; \
	if ! command -v git >/dev/null 2>&1; then \
		echo "ERROR: git not found"; \
		exit 1; \
	fi; \
	if [ ! -d "$(PS2_PACKER_SRC_DIR)/.git" ]; then \
		echo "[ PACK ] fetching ps2-packer source"; \
		rm -rf "$(PS2_PACKER_SRC_DIR)"; \
		mkdir -p "$(dir $(PS2_PACKER_SRC_DIR))"; \
		git clone --depth=1 "$(PS2_PACKER_REPO_URL)" "$(PS2_PACKER_SRC_DIR)"; \
	fi; \
	echo "[ PACK ] building ps2-packer with stubs"; \
	PATH="$(PS2DEV)/bin:$(PS2DEV)/ee/bin:$(PS2DEV)/iop/bin:$(PS2DEV)/dvp/bin:$(PS2SDK)/bin:$$PATH" \
	$(MAKE) -C "$(PS2_PACKER_SRC_DIR)" all CC="$$host_cc" BIN2C="$(BIN2C)" PS2DEV="$(PS2DEV)" PS2SDK="$(PS2SDK)" >/dev/null; \
	test -x "$(PS2_PACKER_LOCAL)" || (echo "ERROR: local ps2-packer was not built"; exit 1); \
	test -f "$(PS2_PACKER_SRC_DIR)/stub/lzma-1d00-stub" || (echo "ERROR: ps2-packer stub was not built"; exit 1)
