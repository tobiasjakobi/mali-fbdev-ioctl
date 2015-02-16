optflags := -O2 -march=armv7-a -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard
compiler := gcc
cflags   := -D_GNU_SOURCE -I/usr/include/libdrm -I/usr/include/exynos
ldflags  := -L$(HOME)/local/lib/mali-r4p0-fbdev -lMali -ldl -ldrm_exynos -ldrm

ifndef platform
platform := $(shell $(compiler) -dumpmachine)
endif

ifeq (release-lto,$(build))
cflags += $(optflags) -flto=4 -fuse-linker-plugin -DNDEBUG
ldflags += $(optflags) -flto=4 -fuse-linker-plugin
endif

ifeq (release,$(build))
cflags += $(optflags) -DNDEBUG
endif

ifeq (debug,$(build))
cflags += -O0 -g
endif

objects := test

all: $(objects)

%.o: %.c
	$(compiler) -c -o $@ $(cflags) $<

test: test.o setup.o; $(compiler) -o $@ $^ $(ldflags)

clean:
	rm -f *.o
	rm -f $(objects)
	rm -f trace.out

strip:
	strip -s $(objects)
