#!/bin/sh
# Builds the firmware. Usage: ./build.sh [setup|update|build] [stuart arguments]
# Needs: clang, lld, llvm, uuid-dev, acpica-tools, gcc-aarch64-linux-gnu, and the
# Python packages of pip-requirements.txt in ./.venv
set -e
cd "$(dirname "$0")"
. .venv/bin/activate
LLVM_BIN="$(llvm-config --bindir 2>/dev/null || echo /usr/lib/llvm-21/bin)/"
export CLANG_BIN="${CLANG_BIN:-$LLVM_BIN}"
export CLANGPDB_BIN="${CLANGPDB_BIN:-$LLVM_BIN}"
export CLANGPDB_AARCH64_PREFIX="${CLANGPDB_AARCH64_PREFIX:-aarch64-linux-gnu-}"
step="${1:-build}"
[ $# -gt 0 ] && shift
if [ "$step" = setup ]; then
  stuart_setup -c Platform/SiliconNTPkg/PlatformBuild.py TOOL_CHAIN_TAG=CLANGPDB "$@"
  # The only nested submodules the build needs: MdePkg.dec and MdeModulePkg.dec name
  # include directories inside them. The others are left out.
  exec git -C MU_BASECORE submodule update --init --depth 1     MdePkg/Library/MipiSysTLib/mipisyst     MdeModulePkg/Library/BrotliCustomDecompressLib/brotli
fi
exec "stuart_$step" -c Platform/SiliconNTPkg/PlatformBuild.py TOOL_CHAIN_TAG=CLANGPDB "$@"
