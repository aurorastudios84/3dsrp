#---------------------------------------------------------------------------------
# n3ds-chiaki smoke test -- builds with devkitARM, links the 3DS cross build of
# chiaki's core from ../chiaki-ng/build-3ds (made by 3ds/build.sh).
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

CHIAKI		:=	$(TOPDIR)/../../chiaki-ng

TARGET		:=	3DSRP
BUILD		:=	build
SOURCES		:=	source
DATA		:=	data
INCLUDES	:=	include

APP_TITLE	:=	3DSRP
APP_DESCRIPTION	:=	PS4/PS5 Remote Play for New Nintendo 3DS
APP_AUTHOR	:=	AuroraStudios84
APP_ICON := $(TOPDIR)/resources/3dsrp-icon.png

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -O2 -mword-relocations \
			-ffunction-sections -fdata-sections \
			$(ARCH)

# ecdh.h sizes ChiakiECDH on CHIAKI_LIB_ENABLE_MBEDTLS (628 B full mbedtls ctx vs
# 8 B stub). The core is built with it, so it MUST be defined here too or
# sizeof(ChiakiSession) mismatches and the core's memset overruns the heap.
CFLAGS	+=	$(INCLUDE) -D__3DS__ -DCHIAKI_LIB_ENABLE_MBEDTLS
CFLAGS += -DN3DS_GPU_BILINEAR
CFLAGS	+=	-I$(TOPDIR)/include
CFLAGS	+=	-I$(CHIAKI)/lib/include
CFLAGS	+=	-I$(CHIAKI)/build-3ds/lib/include
CFLAGS	+=	-I$(CHIAKI)/build-3ds/lib/protobuf
CFLAGS	+=	-I$(CHIAKI)/third-party/nanopb
# chiaki's headers assume IPv6 socket types that libctru does not have
CFLAGS	+=	-include $(CHIAKI)/lib/src/3ds_compat.h

ifdef DEBUG_PAIR
CFLAGS	+=	-DN3DS_RP_DEBUG_PAIR
endif

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

# CTRULIB and PORTLIBS are here for their include/ dirs (libctru + portlibs);
# their lib/ dirs hold the mbedtls/opus/json-c archives we link against.
LIBDIRS	:=	$(TOPDIR)/../usage-tls \
			$(CHIAKI)/build-3ds/lib \
			$(CHIAKI)/build-3ds/third-party \
			$(CHIAKI)/build-3ds/third-party/nanopb \
			$(CTRULIB) \
			$(PORTLIBS)

LIBS	:=	-lchiaki -lprotobuf-nanopb -ljerasure -lgf_complete \
			-ljson-c -lusage_mbedtls -lusage_mbedx509 -lusage_mbedcrypto -lmbedtls -lmbedx509 -lmbedcrypto -lopus \
			-lcitro2d -lcitro3d -lctru -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:=	$(LIBDIRS)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(filter-out replay.c,$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c))))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES		:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

# chiaki's cross build keeps its archives directly in these directories (they
# are not laid out as <dir>/lib), so pass them to the linker literally too.
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib) \
			-L$(CHIAKI)/build-3ds/lib \
			-L$(CHIAKI)/build-3ds/third-party \
			-L$(CHIAKI)/build-3ds/third-party/nanopb

.PHONY: $(BUILD) clean all

#---------------------------------------------------------------------------------
all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf

#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
all	:	$(OUTPUT).3dsx

_3DSXFLAGS := --smdh=$(OUTPUT).smdh
$(OUTPUT).3dsx	:	$(OUTPUT).elf $(OUTPUT).smdh
$(OUTPUT).elf	:	$(OFILES)
$(OUTPUT).smdh : $(TOPDIR)/Makefile $(TOPDIR)/resources/3dsrp-icon.png

#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
usage_tls.o: CFLAGS := -I$(TOPDIR)/../../vendor/usage-mbedtls/include $(CFLAGS)

%.o: %.c
	@echo $(notdir $<)
	$(SILENTCMD)$(CC) -MMD -MP -MF $(DEPSDIR)/$*.d $(CFLAGS) -c $< -o $@

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
