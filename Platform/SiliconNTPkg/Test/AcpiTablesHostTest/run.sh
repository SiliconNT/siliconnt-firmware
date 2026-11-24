#!/bin/sh
# Host test of the ACPI tables the firmware builds at boot. Compiles
# AcpiTablesTest.c, which includes the sources of Drivers/AcpiPlatformDxe that
# build them and the device tree reader, for the host against the Mu headers of
# this tree, and runs it. Then has the ASL compiler build the ASL the test wrote
# next to every DSDT and compares the two.
# Needs: cc, iasl (acpica-tools).
# Usage: ./run.sh [<device tree> <ASL>]
#   With these it also builds the nodes of MTP and SMC from a whole device tree, the
#   raw bytes m1n1 read it as, and compares them with the ASL, which holds the two
#   Device () blocks and nothing else. The tree must be the one of an M4 MacBook Pro
#   (J604) that the tests describe. It is Apple's and is not in this repository.
set -e
[ $# = 0 ] || [ $# = 2 ] || { echo "usage: run.sh [<device tree> <ASL>]" >&2; exit 2; }
[ $# = 0 ] || set -- "$(realpath "$1")" "$(realpath "$2")"
cd "$(dirname "$0")"
root=../../../..
case "$(uname -m)" in
  x86_64) arch=X64 ;;
  aarch64) arch=AArch64 ;;
  *) echo "run.sh: no MdePkg processor headers for host $(uname -m)" >&2; exit 1 ;;
esac
command -v iasl >/dev/null || { echo "run.sh: iasl not found (package acpica-tools)" >&2; exit 1; }
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
# MDEPKG_NDEBUG: no DebugLib. ARM_LIB_H_: skip ArmLib.h, which AppleDTLib.c includes
# without using it and which refuses to compile for anything but ARM.
# -Wno-unused-*: HostTest.h holds more than any one test uses.
${CC:-cc} -Wall -Werror -Wno-unused-function -Wno-unused-variable -Wno-unused-const-variable \
  -fshort-wchar -DMDEPKG_NDEBUG -DARM_LIB_H_ \
  -I "$root/MU_BASECORE/MdePkg/Include" -I "$root/MU_BASECORE/MdePkg/Include/$arch" \
  -I "$root/Common/TIANO/EmbeddedPkg/Include" \
  -I "$root/Silicon/Apple/AppleSiliconPkg/Include" -I ../../Include \
  AcpiTablesTest.c -o "$out/test"
"$out/test" Static816ea14 "$out" "$@"

# What must be the same in the two DSDTs: signature, length and revision (bytes
# 0-8), OEM ID, OEM table ID and OEM revision (10-27), and all of the AML (36 on).
# The checksum (9) and the creator (28-35) are the ASL compiler's own.
failed=0
checked=0
for asl in "$out"/*.asl; do
  name="$(basename "$asl" .asl)"
  if ! iasl -p "$out/$name-reference" "$asl" > "$out/iasl.log" 2>&1; then
    cat "$out/iasl.log"
    echo "FAIL  $name: iasl does not compile the ASL"
    failed=1
    continue
  fi
  if cmp -s -n 9 "$out/$name.aml" "$out/$name-reference.aml" &&
     cmp -s -i 10 -n 18 "$out/$name.aml" "$out/$name-reference.aml" &&
     cmp -s -i 36 "$out/$name.aml" "$out/$name-reference.aml"; then
    echo "ok    $name: the DSDT is what iasl makes of the same ASL"
  else
    echo "FAIL  $name: the DSDT differs from what iasl makes of the same ASL"
    failed=1
  fi
  checked=$((checked + 1))
done
[ "$failed" = 0 ] || exit 1
echo "all $checked DSDTs match the ASL compiler"
