# HiBy R1 InfoNES Makefile
# Cross-compile: make CROSS=mipsel-linux-musl- STATIC=1

CROSS ?= 
CC = $(CROSS)g++
STRIP = $(CROSS)strip

CFLAGS = -O2 -fno-strict-aliasing -fno-lto -Wall -Wextra -Isrc -Isrc/hiby -std=c++11 -D_GNU_SOURCE
LDFLAGS = 
LIBS = -lm -ldl -lpthread

ifeq ($(STATIC),1)
CFLAGS += -EL -march=mips32r2 -mtune=mips32r2
else
LIBS += -lasound
endif

TARGET = infones
CORE_SRCS = src/InfoNES.cpp src/InfoNES_Mapper.cpp src/InfoNES_pAPU.cpp src/K6502.cpp
PLATFORM_SRCS = src/hiby/InfoNES_System_Hiby.cpp src/hiby/font.c src/hiby/platform.c src/hiby/menu.c

SRCS = $(CORE_SRCS) $(PLATFORM_SRCS)
OBJS = $(SRCS:.cpp=.o)
OBJS := $(OBJS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)
	$(STRIP) $@

%.o: %.cpp
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CROSS)gcc -O2 -fno-strict-aliasing -fno-lto -Wall -Isrc/hiby -std=c11 -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

deploy: $(TARGET)
	@echo "=== Deploying to HiBy R1 ==="
	adb root
	adb push $(TARGET) /usr/data/infones
	adb shell chmod +x /usr/data/infones
	@echo "Deployed."
