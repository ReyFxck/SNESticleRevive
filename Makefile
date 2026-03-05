PS2DEV ?= /opt/ps2dev
PS2SDK ?= $(PS2DEV)/ps2sdk
GSKIT  ?= $(PS2DEV)/gsKit

SRC_DIR := $(CURDIR)/src
OBJ_DIR := $(CURDIR)/build
PKG_DIR := $(OBJ_DIR)/pkg
TARGET  := $(OBJ_DIR)/SNESticle.elf

EE_CC    ?= ee-gcc
EE_CXX   ?= ee-g++
EE_STRIP ?= ee-strip

LEGACY_ROOT ?= /root/SNESticle-Beta
IRX_DIR     ?= $(PS2SDK)/iop/irx

CFLAGS := -G0 -O2 -Wall \
	-D_EE -DPS2 -DLSB_FIRST -DALIGN_DWORD -DCODE_PLATFORM=3

CXXFLAGS := -G0 -O2 -Wall -fno-exceptions -fno-rtti \
	-D_EE -DPS2 -DLSB_FIRST -DALIGN_DWORD -DCODE_PLATFORM=3

INCS := \
	-I$(CURDIR)/src/common/media \
	-I$(CURDIR)/src/platform/ps2/cdvd \
	-I$(CURDIR)/src/platform/ps2/gs \
	-I$(CURDIR)/src/platform/ps2/lowlevel \
	-I$(CURDIR)/src/platform/ps2 \
	-I$(CURDIR)/src/platform/ps2/ui \
	-I$(CURDIR)/src/platform/ps2/system \
	-I$(CURDIR)/src/platform/ps2/input \
	-I$(CURDIR)/src/platform/ps2/memcard \
	-I$(CURDIR)/src \
	-I$(LEGACY_ROOT)/Gep/Include/common \
	-I$(LEGACY_ROOT)/Gep/Include/ps2 \
	-I$(LEGACY_ROOT)/Gep/Source/common \
	-I$(LEGACY_ROOT)/Gep/Source/common/zlib \
	-I$(LEGACY_ROOT)/Gep/Include/common/zlib \
	-I$(LEGACY_ROOT)/Gep/Source/common/unzip \
	-I$(LEGACY_ROOT)/Gep/Include/common/unzip \
	-I$(LEGACY_ROOT)/SNESticle/Source/common \
	-I$(LEGACY_ROOT)/SNESticle/Source/ps2 \
	-I$(LEGACY_ROOT)/SNESticle/Modules/mcsave/ee \
	-I$(LEGACY_ROOT)/SNESticle/Modules/sjpcm/ee \
	-I$(LEGACY_ROOT)/SNESticle/Modules/netplay/Source/common \
	-I$(LEGACY_ROOT)/SNESticle/Modules/netplay/Source/ps2/common \
	-I$(LEGACY_ROOT)/SNESticle/Modules/netplay/Source/ps2/ee \
	-I$(LEGACY_ROOT)/SNESticle/Modules/libcdvd/common \
	-I$(LEGACY_ROOT)/SNESticle/Modules/libcdvd/ee \
	-I$(CURDIR)/compat \
	-I$(PS2SDK)/common/include \
	-I$(PS2SDK)/ee/include \
	-I$(PS2SDK)/ports/include

LIBDIRS := \
	-L$(PS2SDK)/ee/lib \
	-L$(PS2SDK)/ports/lib

LIBS := \
	-lmc -lpad -lps2ip \
	-lkernel -lfileXio -lc -lm -ldebug -lstdc++ -lgcc

SRCS := $(shell tr '\n' ' ' < ok-files.txt)

OBJS := \
	$(patsubst src/%.c,$(OBJ_DIR)/%.o,$(filter %.c,$(SRCS))) \
	$(patsubst src/%.cpp,$(OBJ_DIR)/%.o,$(filter %.cpp,$(SRCS))) \
	$(patsubst src/%.s,$(OBJ_DIR)/%.o,$(filter %.s,$(SRCS))) \
	$(patsubst src/%.S,$(OBJ_DIR)/%.o,$(filter %.S,$(SRCS)))

SDK_NET_IRX := ps2dev9.irx netman.irx ps2ip-nm.irx smap.irx
SDK_COMPAT_NET_IRX := ps2ip.irx ps2ips.irx smap-ps2ip.irx
SDK_MC_IRX := mcman.irx mcserv.irx
SDK_EXTRA_IRX := ioptrap.irx poweroff.irx

CUSTOM_IRX_DIR ?= $(CURDIR)/irx
CUSTOM_IRX := NETPLAY.IRX MCSAVE.IRX SJPCM2.IRX

.PHONY: all clean strip list count package package-irx check-env

all: check-env $(TARGET)

check-env:
	@test -d "$(PS2SDK)" || (echo "ERRO: PS2SDK nao encontrado em $(PS2SDK)"; exit 1)
	@test -d "$(IRX_DIR)" || (echo "ERRO: pasta de IRX nao encontrada em $(IRX_DIR)"; exit 1)

$(OBJ_DIR):
	@mkdir -p "$(OBJ_DIR)"

$(PKG_DIR):
	@mkdir -p "$(PKG_DIR)"

$(OBJ_DIR)/%.o: src/%.c | $(OBJ_DIR)
	@mkdir -p "$(dir $@)"
	@echo "CC  $<"
	@$(EE_CC) $(CFLAGS) $(INCS) -c $< -o $@

$(OBJ_DIR)/%.o: src/%.cpp | $(OBJ_DIR)
	@mkdir -p "$(dir $@)"
	@echo "CXX $<"
	@$(EE_CXX) $(CXXFLAGS) $(INCS) -c $< -o $@

$(OBJ_DIR)/%.o: src/%.s | $(OBJ_DIR)
	@mkdir -p "$(dir $@)"
	@echo "AS  $<"
	@$(EE_CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: src/%.S | $(OBJ_DIR)
	@mkdir -p "$(dir $@)"
	@echo "AS  $<"
	@$(EE_CC) $(CFLAGS) $(INCS) -c $< -o $@

# input.c esta sendo tratado como C++ no seu build atual
$(OBJ_DIR)/platform/ps2/input/input.o: src/platform/ps2/input/input.c | $(OBJ_DIR)
	@mkdir -p "$(dir $@)"
	@echo "CXX $<"
	@$(EE_CXX) -x c++ $(CXXFLAGS) $(INCS) -c $< -o $@

$(TARGET): $(OBJS) | $(OBJ_DIR)
	@echo "LD  $@"
	@$(EE_CXX) -o $@ $(OBJS) $(LIBDIRS) $(LIBS)

strip: $(TARGET)
	@echo "STRIP $<"
	@$(EE_STRIP) "$(TARGET)"

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
	copy_custom() { \
		f="$$1"; \
		if [ -f "$(CUSTOM_IRX_DIR)/$$f" ]; then \
			cp "$(CUSTOM_IRX_DIR)/$$f" "$(PKG_DIR)/"; \
			echo "  + $$(basename "$(CUSTOM_IRX_DIR)")/$$f"; \
		else \
			echo "  ! faltando custom IRX $$f (procurei em $(CUSTOM_IRX_DIR))"; \
		fi; \
	}; \
	echo "== SDK network IRX =="; \
	for f in $(SDK_NET_IRX); do copy_sdk "$$f"; done; \
	echo "== SDK compat network IRX =="; \
	for f in $(SDK_COMPAT_NET_IRX); do copy_sdk "$$f"; done; \
	echo "== SDK memory card IRX =="; \
	for f in $(SDK_MC_IRX); do copy_sdk "$$f"; done; \
	echo "== SDK extra IRX =="; \
	for f in $(SDK_EXTRA_IRX); do copy_sdk "$$f"; done; \
	echo "== Project custom IRX =="; \
	for f in $(CUSTOM_IRX); do copy_custom "$$f"; done; \
	echo "Pronto: $(PKG_DIR)"
clean:
	rm -rf "$(OBJ_DIR)"

list:
	@printf '%s\n' $(SRCS)

count:
	@printf 'sources: %s\n' "$(words $(SRCS))"
	@printf 'objects: %s\n' "$(words $(OBJS))"


# ---------------- ISO (CD/DVD) ----------------
ISO_LABEL ?= SNESTICLE
ISO_OUT   ?= $(OBJ_DIR)/$(ISO_LABEL).iso
ISO_DIR   ?= $(OBJ_DIR)/iso_root
BOOT_ELF  ?= $(ISO_LABEL).ELF
VMODE     ?= NTSC
VER       ?= 1.00

# tenta achar uma ferramenta tipo mkisofs
MKISOFS ?= $(shell if command -v xorriso >/dev/null 2>&1; then echo "xorriso -as mkisofs"; \
	elif command -v genisoimage >/dev/null 2>&1; then echo "genisoimage"; \
	elif command -v mkisofs >/dev/null 2>&1; then echo "mkisofs"; \
	else echo "mkisofs"; fi)

MKISOFSFLAGS ?= -J -R -l -iso-level 2

.PHONY: iso iso_stage iso_image

iso: package iso_stage iso_image
	@echo "ISO pronta: $(ISO_OUT)"

iso_stage: | $(OBJ_DIR)
	@set -e; \
	rm -rf "$(ISO_DIR)"; \
	mkdir -p "$(ISO_DIR)"; \
	echo "[ISO] copiando arquivos do pkg..."; \
	cp -a "$(PKG_DIR)/." "$(ISO_DIR)/"; \
		if [ -d "$(CURDIR)/cdroot" ]; then \
			echo "[ISO] copiando cdroot extras..."; \
			cp -a "$(CURDIR)/cdroot/." "$(ISO_DIR)/"; \
		fi; \
	# renomeia o ELF de boot para MAIUSCULO (padrao de disco)
	if [ -f "$(ISO_DIR)/SNESticle.elf" ]; then \
		mv "$(ISO_DIR)/SNESticle.elf" "$(ISO_DIR)/$(BOOT_ELF)"; \
	elif [ -f "$(ISO_DIR)/SNESticle.ELF" ]; then \
		mv "$(ISO_DIR)/SNESticle.ELF" "$(ISO_DIR)/$(BOOT_ELF)"; \
	fi; \
	# cria SYSTEM.CNF (na raiz) - ordem BOOT2/VER/VMODE
	printf "BOOT2 = cdrom0:\\%s;1\r\nVER = %s\r\nVMODE = %s\r\n" "$(BOOT_ELF)" "$(VER)" "$(VMODE)" > "$(ISO_DIR)/SYSTEM.CNF"; \
	echo "[ISO] SYSTEM.CNF:"; \
	cat "$(ISO_DIR)/SYSTEM.CNF"; \
	echo "[ISO] ISO_DIR=$(ISO_DIR)"

iso_image:
	@set -e; \
	echo "[ISO] gerando $(ISO_OUT) com: $(MKISOFS) $(MKISOFSFLAGS)"; \
	$(MKISOFS) $(MKISOFSFLAGS) -V "$(ISO_LABEL)" -o "$(ISO_OUT)" "$(ISO_DIR)"; \
	# se existir ps2bootgen no sistema, roda (opcional)
	if command -v ps2bootgen >/dev/null 2>&1; then \
		echo "[ISO] ps2bootgen detectado: aplicando licenca (opcional)"; \
		ps2bootgen -dvd -japan "$(ISO_OUT)"; \
	else \
		echo "[ISO] ps2bootgen nao encontrado (ok para OPL/emulador)"; \
	fi
# -------------- /ISO -----------------
