FROM --platform=linux/amd64 debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    make \
    wget \
    ca-certificates \
    xz-utils \
    bzip2 \
    && rm -rf /var/lib/apt/lists/*

# Use the official Ingenic toolchain (copied from build context)
COPY mips-gcc520-glibc222-64bit-r3.2.1.tar.xz /tmp/
RUN tar xf /tmp/mips-gcc520-glibc222-64bit-r3.2.1.tar.xz -C /opt \
    && rm /tmp/mips-gcc520-glibc222-64bit-r3.2.1.tar.xz

ENV PATH="/opt/ingenic-toolchain/mips-gcc520-glibc222/bin:${PATH}"
# Let's verify what glibc version this toolchain uses.
RUN mips-linux-gnu-gcc --version

# Cross-compile zlib
ENV ZLIB_VERSION=1.3.1
RUN wget -q https://github.com/madler/zlib/releases/download/v${ZLIB_VERSION}/zlib-${ZLIB_VERSION}.tar.gz \
    && tar xf zlib-${ZLIB_VERSION}.tar.gz \
    && cd zlib-${ZLIB_VERSION} \
    && CC=mips-linux-gnu-gcc \
       AR=mips-linux-gnu-ar \
       RANLIB=mips-linux-gnu-ranlib \
       CFLAGS="-O2" \
       ./configure --prefix=/opt/mipsel-libs --static \
    && make -j$(nproc) \
    && make install \
    && cd / \
    && rm -rf zlib-${ZLIB_VERSION} zlib-${ZLIB_VERSION}.tar.gz

# Cross-compile FFmpeg
ENV FFMPEG_VERSION=7.1.1
RUN wget -q https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz \
    && tar xf ffmpeg-${FFMPEG_VERSION}.tar.xz \
    && cd ffmpeg-${FFMPEG_VERSION} \
    && ./configure \
        --prefix=/opt/ffmpeg-mipsel \
        --arch=mips \
        --target-os=linux \
        --cross-prefix=mips-linux-gnu- \
        --enable-cross-compile \
        --enable-static \
        --disable-shared \
        --disable-programs \
        --disable-doc \
        --disable-debug \
        --disable-encoders \
        --disable-muxers \
        --disable-devices \
        --disable-filters \
        --disable-network \
        --disable-hwaccels \
        --enable-swscale \
        --enable-zlib \
        --enable-small \
        --extra-cflags="-O2 -I/opt/mipsel-libs/include" \
        --extra-ldflags="-L/opt/mipsel-libs/lib" \
        --disable-asm \
        --disable-inline-asm \
        --disable-mipsdsp \
        --disable-mipsdspr2 \
        --disable-msa \
        --disable-mmi \
        --disable-loongson2 \
        --disable-loongson3 \
    && make -j$(nproc) \
    && make install \
    && cd / \
    && rm -rf ffmpeg-${FFMPEG_VERSION} ffmpeg-${FFMPEG_VERSION}.tar.xz

WORKDIR /build
