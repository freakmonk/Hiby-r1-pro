# Makefile for Audiobook standalone app

# We use zig cc to cross-compile against glibc 2.22 (the Hiby R1's libc).
# This is required because the player engine uses dlopen() to load libasound.so
# and libjpeg.so dynamically at runtime, so static linking is not viable,
# and standard cross-compilers like Debian's mipsel-linux-gnu-gcc target
# newer glibc versions (e.g. 2.36) which will crash on the device.

ZIG ?= zig

TARGET = mipsel-linux-gnueabihf.2.22
CFLAGS = -O2 -s -DSTANDALONE -Ivendor -Ivendor -Ivendor/libjpeg
LDFLAGS = -lpthread -ldl -lm

SRCS = main.c ui.c render.c font.c library.c scan.c tags.c mp4_audio.c cover.c pngdec.c bookmark_sd.c storage_guard.c bluetooth.c music_catalog.c player.c wsola.c sqlite3.c

SQLITE_FLAGS = -DSQLITE_THREADSAFE=2 -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_OMIT_LOAD_EXTENSION=1 -DSQLITE_ENABLE_FTS5=1 -DSQLITE_OMIT_DEPRECATED=1 -DSQLITE_TEMP_STORE=2

all: Audiobook

Audiobook: $(SRCS)
	$(ZIG) cc -target $(TARGET) $(CFLAGS) $(SQLITE_FLAGS) $(SRCS) -o Audiobook $(LDFLAGS)

clean:
	rm -f Audiobook
