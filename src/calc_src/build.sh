#!/bin/bash
docker build -t hiby-builder -f ../Dockerfile.mips ..
docker run --rm -v "$(pwd):/build" hiby-builder make
