#!/bin/sh
# Host test of Drivers/SimpleFbDxe: compiles SimpleFbDxeTest.c, which includes
# Drivers/SimpleFbDxe/SimpleFbDxe.c and MdeModulePkg's FrameBufferBltLib.c, for
# the host against the Mu headers of this tree, and runs it.
# Needs: cc. Usage: ./run.sh
set -e
cd "$(dirname "$0")"
root=../../../../..
case "$(uname -m)" in
  x86_64) arch=X64 ;;
  aarch64) arch=AArch64 ;;
  *) echo "run.sh: no MdePkg processor headers for host $(uname -m)" >&2; exit 1 ;;
esac
out="$(mktemp)"
trap 'rm -f "$out"' EXIT
# MDEPKG_NDEBUG: no DebugLib. -Wno-unused-*: the drawing library has more than
# the test calls.
${CC:-cc} -Wall -Werror -Wno-unused-function -Wno-unused-variable -Wno-unused-const-variable \
  -fshort-wchar -DMDEPKG_NDEBUG \
  -I "$root/MU_BASECORE/MdePkg/Include" -I "$root/MU_BASECORE/MdePkg/Include/$arch" \
  -I "$root/MU_BASECORE/MdeModulePkg/Include" \
  -I ../../Include \
  SimpleFbDxeTest.c -o "$out"
"$out"
