CC = mipsel-linux-gnu-gcc
CFLAGS = -Wall -O2 -march=mips32r2 -mtune=mips32r2 -fno-pie
LDFLAGS = -static -no-pie -lm

all: hicmndr

hicmndr: hicmndr.c
	$(CC) $(CFLAGS) -o hicmndr hicmndr.c $(LDFLAGS)

clean:
	rm -f hicmndr
