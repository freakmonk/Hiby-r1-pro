CC = mips-linux-gnu-gcc
CFLAGS = -Wall -O2 -s -I/opt/ffmpeg-mipsel/include -I/opt/mipsel-libs/include
LDFLAGS = -L/opt/ffmpeg-mipsel/lib -L/opt/mipsel-libs/lib \
          -lavformat -lavcodec -lswscale -lswresample -lavutil \
          -lz -lm -lpthread -ldl

all: tube

tube: main.c
	$(CC) $(CFLAGS) -o tube main.c $(LDFLAGS)

clean:
	rm -f tube
