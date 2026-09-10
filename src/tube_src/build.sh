#!/bin/bash
set -e

# Copy toolchain for Docker build context if not exists
if [ ! -f "mips-gcc520-glibc222-64bit-r3.2.1.tar.xz" ]; then
    cp ../mplayer/mips-gcc520-glibc222-64bit-r3.2.1.tar.xz .
fi

docker build --progress=plain -t hiby-tube-builder .
docker run --rm -v "$(pwd):/build" hiby-tube-builder sh -c '
  make clean all
  ls -lh tube
  echo "=== Binary info ==="
  readelf -h tube | grep -E "Machine|Class|Flags" || true
'
