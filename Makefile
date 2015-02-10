optflags := -O2 -march=armv7-a -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard
compiler := gcc
cflags   := 
ldflags  := -L$(HOME)/local/lib/mali-r4p0-fbdev -lMali

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

%.o: src/%.c
	$(compiler) -c -o $@ $(cflags) $<

test: test.o; $(compiler) -o $@ $^ $(ldflags)

clean:
	rm -f *.o
	rm -f $(objects)
	rm -f trace.out

strip:
	strip -s $(objects)
