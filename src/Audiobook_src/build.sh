#!/bin/bash
set -e

if ! command -v zig &> /dev/null; then
    echo "Zig not found in PATH."
    if [ "$(uname)" = "Darwin" ]; then
        echo "Please install Zig: brew install zig"
        exit 1
    else
        echo "Please install Zig from ziglang.org"
        exit 1
    fi
fi

make
