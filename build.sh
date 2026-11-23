#!/bin/sh
# Build the freestanding `site` binary. No libc, no dynamic loader:
# raw syscalls only, so the same file runs on any x86_64 Linux.
set -e
cd "$(dirname "$0")"
gcc -O3 -march=x86-64 -nostdlib -static -no-pie -fno-pie \
  -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -s \
  -o site start.S rt.c text.c map.c json.c frontmatter.c markdown.c model.c template.c build.c main.c
