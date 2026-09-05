#!/bin/bash
set -e
docker build --progress=plain -t hiby-mplayer-builder .
docker run --rm -v "$(pwd):/build" hiby-mplayer-builder sh -c '
  make clean all
  ls -lh mplayer
  echo "=== Binary info ==="
  file mplayer
  readelf -l mplayer | grep -i interp || true
  readelf -d mplayer | grep NEEDED || true
  readelf -h mplayer | grep -E "Machine|Class|Flags" || true
'
