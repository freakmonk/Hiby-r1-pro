#!/bin/bash
set -e
docker run --rm -v "$(pwd):/build" hiby-mplayer-builder sh -c '
  make clean all
'
