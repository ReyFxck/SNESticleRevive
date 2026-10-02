IOP_BIN = rfauds2.irx

IOP_IMPORT_INCS += \
    system/intrman \
    system/loadcore \
    system/sifcmd \
    system/threadman

IOP_INCS += -I../../include
IOP_OBJS = main.o spu2_direct.o imports.o

PS2SDKSRC ?= $(PS2SDK)

include $(PS2SDKSRC)/Defs.make
include $(PS2SDKSRC)/iop/Rules.bin.make
include $(PS2SDKSRC)/iop/Rules.make
