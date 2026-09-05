CROSS_COMPILE ?= 
CC = $(CROSS_COMPILE)gcc
STRIP = $(CROSS_COMPILE)strip

SOURCES = $(patsubst src/%, %, $(wildcard src/*.c))
SOURCES += device/main.c device/i_hiby_video.c device/i_hiby_input.c device/i_hiby_sound.c device/i_no_music.c
OBJECTS = $(patsubst %.c, %.o, $(SOURCES))
TARGET_OBJS = $(patsubst %, build/%, $(OBJECTS))

CCFLAGS = -DNORMALUNIX -std=gnu99 -O2 -Wall -Wno-unused-result
ifneq ($(CROSS_COMPILE),)
CCFLAGS += -march=mips32r2 -mtune=mips32r2
LDFLAGS += -ldl -lpthread
endif

.PHONY: all clean link

all: doom

build:
	mkdir -p build/device

build/%.o: src/%.c
	@mkdir -p build/device
	@echo "Compiling $<..."
	$(CC) -c $< -I src $(CCFLAGS) -o $@

build/device/%.o: src/device/%.c
	@mkdir -p build/device
	@echo "Compiling $<..."
	$(CC) -c $< -I src $(CCFLAGS) -o $@

link: $(TARGET_OBJS)
	@echo "Linking doom..."
	$(CC) -o doom $(TARGET_OBJS) $(LDFLAGS) -lm
ifneq ($(CROSS_COMPILE),)
	$(STRIP) doom
endif

doom: link

clean:
	rm -rf build doom
