CROSS_COMPILE ?= /opt/ingenic-toolchain/mips-gcc520-glibc222/bin/mips-linux-gnu-
CC = $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++
STRIP = $(CROSS_COMPILE)strip

SOURCES = src/bank.cpp src/engine.cpp src/file.cpp src/main.cpp src/mixer.cpp src/parts.cpp \
          src/resource.cpp src/serializer.cpp src/sfxplayer.cpp src/staticres.cpp src/util.cpp src/video.cpp src/vm.cpp \
          src/sysImplementationHiby.cpp
OBJECTS = $(patsubst %.cpp, build/%.o, $(SOURCES))

CXXFLAGS = -Os -std=c++11 -DSYS_LITTLE_ENDIAN -Wall -Wno-unknown-pragmas -Wshadow -Wundef -Wwrite-strings -Wnon-virtual-dtor -Wno-multichar -march=mips32r2 -mtune=mips32r2
LDFLAGS = -ldl -lpthread -lrt -L /opt/mipsel-libs/lib -lz

.PHONY: all clean

all: raw

build/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	$(CXX) -c $< -I src -I /opt/mipsel-libs/include $(CXXFLAGS) -o $@

raw: $(OBJECTS)
	@echo "Linking raw..."
	$(CXX) -o raw $(OBJECTS) $(LDFLAGS)
	$(STRIP) raw

clean:
	rm -rf build raw
