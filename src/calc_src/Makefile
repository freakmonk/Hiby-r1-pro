CC = mipsel-linux-gnu-gcc
CFLAGS = -Wall -O2 -march=mips32r2 -mtune=mips32r2 -fno-pie
LDFLAGS = -static -no-pie -lm

all: calc

calc: main.c
	$(CC) $(CFLAGS) -o calc main.c $(LDFLAGS)

clean:
	rm -f calc
