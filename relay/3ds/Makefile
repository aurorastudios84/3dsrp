ifeq ($(strip $(DEVKITPRO)),)
$(error Set DEVKITPRO)
endif
ROOT := $(abspath ../..)
SDK := $(DEVKITPRO)
CC := $(SDK)/devkitARM/bin/arm-none-eabi-gcc
ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS := $(ARCH) -O3 -g -std=gnu11 -D__3DS__ -Wall -Wextra -Wno-unused-parameter -mword-relocations -ffunction-sections -fdata-sections -I. -I../common -I$(ROOT)/relay/3ds/shared/include -I$(ROOT)/relay/vendor/chiaki-upstream/lib/include -I$(ROOT)/relay/android/app/src/main/cpp/generated -Ibuild/jpeg -I../vendor/libjpeg-turbo-3.1.3/src -I$(SDK)/libctru/include -I$(SDK)/portlibs/3ds/include -MMD -MP
LDFLAGS := $(ARCH) -specs=$(ROOT)/relay/3ds/old3ds.specs -L$(SDK)/libctru/lib -L$(SDK)/portlibs/3ds/lib -Wl,--gc-sections,-Map,build/relay.map,--wrap=linearAlloc,--wrap=linearMemAlign,--wrap=linearFree
OBJECTS := build/main.o build/graphics_cache.o build/linear_lock.o build/controls.o build/input_3ds.o build/motion_3ds.o build/controller.o build/orientation.o build/usage_relay.o build/usage_stats.o build/usage_preferences.o build/usage_journal.o build/pair_store.o build/usage_platform_3ds.o
all: ../output/old3ds-relay.3dsx
build ../output:
	mkdir -p $@
build/%.o: %.c | build
	$(CC) $(CFLAGS) -c $< -o $@
../output/old3ds-relay.elf: $(OBJECTS) build/jpeg/libjpeg.a | ../output
	$(CC) $(LDFLAGS) $(OBJECTS) build/jpeg/libjpeg.a -lcitro2d -lcitro3d -lctru -lm -o $@
../output/old3ds-relay.smdh: $(ROOT)/source/3ds/app/resources/3dsrp-icon.png | ../output
	$(SDK)/tools/bin/smdhtool --create '3DSRP Relay' 'Remote Play via Android or Windows' 'AuroraStudios84' $< $@
../output/old3ds-relay.3dsx: ../output/old3ds-relay.elf ../output/old3ds-relay.smdh
	$(SDK)/tools/bin/3dsxtool $< $@ --smdh=../output/old3ds-relay.smdh
-include $(OBJECTS:.o=.d)

build/input_3ds.o: $(ROOT)/relay/3ds/shared/source/input_3ds.c | build
	$(CC) $(CFLAGS) -c $< -o $@
build/motion_3ds.o: motion_3ds.c | build
	$(CC) $(CFLAGS) -c $< -o $@
build/controller.o: $(ROOT)/relay/vendor/chiaki-upstream/lib/src/controller.c | build
	$(CC) $(CFLAGS) -c $< -o $@
build/orientation.o: $(ROOT)/relay/vendor/chiaki-upstream/lib/src/orientation.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/usage_stats.o build/usage_preferences.o build/usage_journal.o build/pair_store.o build/usage_platform_3ds.o: build/%.o: $(ROOT)/relay/3ds/shared/source/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@
