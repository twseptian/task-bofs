#!/usr/bin/env bash
# build.sh — compile the BOF suite into COFF objects for distribution
# Outputs land in the sibling extension folders, ready for `extensions load`.
#
# Requirements: mingw-w64 cross compiler (x86_64-w64-mingw32-gcc).
#
# IMPORTANT FLAGS:
#   -fno-builtin   GCC -O2 otherwise rewrites strlen-like loops into CRT
#                  calls (and -fno-builtin exposes ZeroMemory->memset, which
#                  is why bofutil.h ships my_memzero). Any unresolved symbol
#                  other than Beacon* / __imp_* means the BOF will not load.
#   -Wall          catch mistakes early.
set -euo pipefail
cd "$(dirname "$0")"

mkdir -p ../taskenum ../tasksuite

build_bof () {
    local src="$1" out="$2"
    echo "[*] building ${src} (x64)..."
    x86_64-w64-mingw32-gcc -c "${src}" -I. \
        -o "${out}" \
        -O2 -DBOF -fno-builtin -Wall
}

build_bof taskenum.c    ../taskenum/taskenum.x64.o
build_bof taskhijack.c  ../tasksuite/taskhijack.x64.o
build_bof taskrestore.c ../tasksuite/taskrestore.x64.o

echo "[+] built:"
ls -la ../taskenum/ ../tasksuite/

echo "[*] audit: unresolved symbols must ONLY be Beacon* / __imp_*"
for o in ../taskenum/taskenum.x64.o ../tasksuite/*.x64.o; do
    nm "$o" | grep ' U ' | grep -vE 'Beacon|__imp_' && { echo "!!! CRT LEAK: $o"; exit 1; }
    nm "$o" | grep -q ' T go' || { echo "!!! missing entry: $o"; exit 1; }
done
echo "[+] audit clean"
