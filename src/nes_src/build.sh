#!/bin/bash
set -e

# Use the same builder image that mplayer uses
docker run --rm -v "$(pwd):/build" hiby-mplayer-builder sh -c '
  make clean
  make CROSS=mips-linux-gnu- STATIC=1
  echo "=== Binary info ==="
  mips-linux-gnu-readelf -h infones | grep -E "Machine|Class|Flags|Data" || true
'
