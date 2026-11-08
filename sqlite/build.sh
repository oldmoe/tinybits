#!/bin/bash
# builds sqlite/tinybits.so against the amalgamated header in ../dist
set -e
cd "$(dirname "$0")"
gcc -O3 -Wall -Wextra -fPIC -shared -I/usr/local/include -o tinybits.so.tmp tinybits_sqlite.c -lm
mv tinybits.so.tmp tinybits.so
echo "built $(pwd)/tinybits.so"
