#!/bin/sh
# Host tests of Library/AppleDTLib: compiles each test, which includes
# Library/AppleDTLib/AppleDTLib.c through HostTest.h, for the host against the Mu
# headers of this tree, and runs it.
#   BootRamdiskTest.c   AppleGetBootRamdisk()
#   MachineInfoTest.c   AppleGetCpus(), AppleGetUartBase() (also on the tree m1n1 hands over
#                       without a host), AppleGetMmioSpan(), AppleGetChipId()
#   AicToGicTest.c      AppleGetAicToGic(), AppleFindGicInterrupt()
#   AnsTest.c           AppleGetAns(), AppleGetAnsSartVersion(), AppleGetAnsPmgr()
#   CoprocessorTest.c   AppleGetMtp(), AppleGetSmc()
# and of what Library/AppleUartSerialPortLib answers with a serial port and without:
#   SerialPortLibTest.c
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
for test in BootRamdiskTest MachineInfoTest AicToGicTest AnsTest CoprocessorTest SerialPortLibTest; do
  echo "== $test"
  # MDEPKG_NDEBUG: no DebugLib. ARM_LIB_H_: skip ArmLib.h, which AppleDTLib.c and
  # AppleUartSerialPortLib.c include and which refuses to compile for anything but
  # ARM. AARCH64_H_: skip AArch64.h, which AppleUartSerialPortLib.c includes without
  # using it; its directory comes after the host's, which holds ProcessorBind.h.
  # -Wno-unused-*: HostTest.h holds more than any one test uses.
  ${CC:-cc} -Wall -Werror -Wno-unused-function -Wno-unused-variable -Wno-unused-const-variable \
    -fshort-wchar -DMDEPKG_NDEBUG -DARM_LIB_H_ -DAARCH64_H_ \
    -I "$root/MU_BASECORE/MdePkg/Include" -I "$root/MU_BASECORE/MdePkg/Include/$arch" \
    -I "$root/MU_BASECORE/MdePkg/Include/AArch64" \
    -I ../../Include \
    "$test.c" -o "$out"
  "$out"
done
