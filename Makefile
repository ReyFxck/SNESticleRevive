PS2DEV ?= /opt/ps2dev
PS2SDK ?= $(PS2DEV)/ps2sdk

SRC_DIR := $(CURDIR)/src
OBJ_DIR := $(CURDIR)/build
TARGET  := $(OBJ_DIR)/SNESticle.elf

EE_CC  := ee-gcc
EE_CXX := ee-g++

CFLAGS   := -G0 -O2 -Wall -D_EE -DPS2 -DLSB_FIRST -DALIGN_DWORD -DCODE_PLATFORM=3
CXXFLAGS := -G0 -O2 -Wall -fno-exceptions -fno-rtti -D_EE -DPS2 -DLSB_FIRST -DALIGN_DWORD -DCODE_PLATFORM=3

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
-I/root/SNESticle-Beta/Gep/Include/common \
-I/root/SNESticle-Beta/Gep/Include/ps2 \
-I/root/SNESticle-Beta/Gep/Source/common \
-I/root/SNESticle-Beta/Gep/Source/common/zlib \
-I/root/SNESticle-Beta/Gep/Include/common/zlib \
-I/root/SNESticle-Beta/Gep/Source/common/unzip \
-I/root/SNESticle-Beta/Gep/Include/common/unzip \
-I/root/SNESticle-Beta/SNESticle/Source/common \
-I/root/SNESticle-Beta/SNESticle/Source/ps2 \
-I/root/SNESticle-Beta/SNESticle/Modules/mcsave/ee \
-I/root/SNESticle-Beta/SNESticle/Modules/sjpcm/ee \
-I/root/SNESticle-Beta/SNESticle/Modules/netplay/Source/common \
-I/root/SNESticle-Beta/SNESticle/Modules/netplay/Source/ps2/common \
-I/root/SNESticle-Beta/SNESticle/Modules/netplay/Source/ps2/ee \
-I/root/SNESticle-Beta/SNESticle/Modules/libcdvd/common \
-I/root/SNESticle-Beta/SNESticle/Modules/libcdvd/ee \
-I$(CURDIR)/compat \
-I$(PS2SDK)/common/include \
-I$(PS2SDK)/ee/include \
-I$(PS2SDK)/ports/include

LIBDIRS := \
-L$(PS2SDK)/ee/lib \
-L$(PS2SDK)/ports/lib

LIBS := -lmc -lpad -lps2ip -lkernel -lfileXio -lc -lm -ldebug -lstdc++ -lgcc

SRCS := $(shell cat ok-files.txt)

OBJS := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(filter %.c,$(SRCS))) \
        $(patsubst src/%.cpp,$(OBJ_DIR)/%.o,$(filter %.cpp,$(SRCS))) \
        $(patsubst src/%.s,$(OBJ_DIR)/%.o,$(filter %.s,$(SRCS))) \
        $(patsubst src/%.S,$(OBJ_DIR)/%.o,$(filter %.S,$(SRCS)))

all: $(OBJ_DIR) $(TARGET)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)
$(OBJ_DIR)/%.o: src/%.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	@echo "CC  $<"
	@$(EE_CC) $(CFLAGS) $(INCS) -c $< -o $@

$(OBJ_DIR)/%.o: src/%.cpp | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	@echo "CXX $<"
	@$(EE_CXX) $(CXXFLAGS) $(INCS) -c $< -o $@

$(OBJ_DIR)/%.o: src/%.s | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	@echo "AS  $<"
	@$(EE_CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: src/%.S | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	@echo "AS  $<"
	@$(EE_CC) $(CFLAGS) $(INCS) -c $< -o $@

$(OBJ_DIR)/platform/ps2/input/input.o: src/platform/ps2/input/input.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	@echo "CXX $<"
	@$(EE_CXX) -x c++ $(CXXFLAGS) $(INCS) -c $< -o $@

$(TARGET): $(OBJS)
	@echo "LD  $@"
	$(EE_CXX) -o $@ $(OBJS) $(LIBDIRS) $(LIBS)

clean:
	rm -rf $(OBJ_DIR)

list:
	@printf '%s\n' $(SRCS)

count:
	@printf 'sources: %s\n' "$(words $(SRCS))"
	@printf 'objects: %s\n' "$(words $(OBJS))"
