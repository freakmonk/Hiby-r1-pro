CC = mips-linux-gnu-gcc
CFLAGS = -Wall -O2 -s -I/opt/ffmpeg-mipsel/include -I/opt/mipsel-libs/include
LDFLAGS = -L/opt/ffmpeg-mipsel/lib -L/opt/mipsel-libs/lib \
          -lavformat -lavcodec -lswscale -lswresample -lavutil \
          -lz -lm -lpthread -ldl

all: mplayer hello

hello: hello.c
	$(CC) -Wall -O2 -s -o hello hello.c

mplayer: main.c
	$(CC) $(CFLAGS) -o mplayer main.c $(LDFLAGS)

clean:
	rm -f mplayer hello
