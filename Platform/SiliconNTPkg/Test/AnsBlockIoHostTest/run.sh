#!/bin/sh
# Host test of the internal disk driver (Drivers/AnsBlockIoDxe). Compiles
# AnsBlockIoTest.c, which includes the driver's own sources and the device tree
# reader, for the host against the Mu headers of this tree; links it with the
# disk controller's core and the fake controller from the siliconnt-drivers
# submodule, each file as it is there; runs it with AddressSanitizer and
# UndefinedBehaviorSanitizer.
# Needs: cc with the sanitizer libraries, the submodule checked out.
# Usage: ./run.sh [-v]
set -e
cd "$(dirname "$0")"
root=../../../..
drivers=../../Drivers/AnsBlockIoDxe/siliconnt-drivers
core=$drivers/ans/core
case "$(uname -m)" in
  x86_64) arch=X64 ;;
  aarch64) arch=AArch64 ;;
  *) echo "run.sh: no MdePkg processor headers for host $(uname -m)" >&2; exit 1 ;;
esac
[ -f "$core/ans_core.h" ] || {
  echo "run.sh: $core is missing; run: git submodule update --init Platform/SiliconNTPkg/Drivers/AnsBlockIoDxe/siliconnt-drivers" >&2
  exit 1
}
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
san="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"

# The core and the fake, as the drivers repository's own model test builds them
for f in "$core"/*.c "$core/test/fake_ans.c"; do
  ${CC:-cc} -std=c11 -Wall -Werror -O1 -g $san -c "$f" -o "$out/$(basename "$f" .c).o"
done

# NO_MSABI_VA_FUNCS: the host's own va_list for VA_LIST, as EFIAPI is the host's
# calling convention here. ARM_LIB_H_: skip ArmLib.h, which AppleDTLib.c includes
# without using it and which refuses to compile for anything but ARM.
# -Wno-unused-*: HostTest.h holds more than any one test uses.
${CC:-cc} -Wall -Werror -Wno-unused-function -Wno-unused-variable -Wno-unused-const-variable \
  -O1 -g $san -fshort-wchar -DNO_MSABI_VA_FUNCS -DARM_LIB_H_ \
  -I "$root/MU_BASECORE/MdePkg/Include" -I "$root/MU_BASECORE/MdePkg/Include/$arch" \
  -I "$root/Common/TIANO/EmbeddedPkg/Include" \
  -I "$root/Silicon/Apple/AppleSiliconPkg/Include" -I ../../Include \
  -c AnsBlockIoTest.c -o "$out/AnsBlockIoTest.o"
${CC:-cc} $san "$out"/*.o -o "$out/test"
"$out/test" "$@"
