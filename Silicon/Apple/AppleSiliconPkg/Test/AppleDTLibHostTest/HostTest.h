/** @file
  What the host tests of the device tree reader share: stand-ins for the
  firmware's libraries, Library/AppleDTLib itself, a builder of Apple device
  trees, and the tree of one real machine.

  A test includes this file once. The tests of other packages that start from a
  device tree include it too.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef APPLE_DT_HOST_TEST_H_
#define APPLE_DT_HOST_TEST_H_

#include <Base.h>
#include <PiPei.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/HobLib.h>
#include <Library/AppleDTLib.h>

#include <stdio.h>
#include <string.h>

//
// What AppleDTLib.c takes from the firmware's libraries
//
EFI_GUID  gAppleSiliconPkgBootInfoHobGuid;

VOID *EFIAPI GetFirstGuidHob(IN CONST EFI_GUID *Guid)
{
    return NULL;
}

INTN EFIAPI AsciiStrCmp(IN CONST CHAR8 *First, IN CONST CHAR8 *Second)
{
    return strcmp(First, Second);
}

INTN EFIAPI AsciiStrnCmp(IN CONST CHAR8 *First, IN CONST CHAR8 *Second, IN UINTN Length)
{
    return strncmp(First, Second, Length);
}

CHAR8 *EFIAPI AsciiStrStr(IN CONST CHAR8 *String, IN CONST CHAR8 *SearchString)
{
    return (CHAR8 *)strstr(String, SearchString);
}

UINTN EFIAPI AsciiStrLen(IN CONST CHAR8 *String)
{
    return strlen(String);
}

VOID *EFIAPI CopyMem(OUT VOID *Destination, IN CONST VOID *Source, IN UINTN Length)
{
    return memmove(Destination, Source, Length);
}

VOID *EFIAPI ZeroMem(OUT VOID *Buffer, IN UINTN Length)
{
    return memset(Buffer, 0, Length);
}

//
// The library itself. Included, not compiled on its own: the firmware build
// declares the GUID above in a generated header.
//
#include "../../Library/AppleDTLib/AppleDTLib.c"

//
// Device tree builder. A node is two counts, then its properties (32-byte
// name, 32-bit length, value padded to 4 bytes), then its children.
//
STATIC UINT8  mTree[16384];
STATIC UINTN  mUsed;

STATIC VOID Node(IN UINT32 Properties, IN UINT32 Children)
{
    memcpy(mTree + mUsed, &Properties, 4);
    memcpy(mTree + mUsed + 4, &Children, 4);
    mUsed += 8;
}

STATIC VOID Prop(IN CONST CHAR8 *Key, IN CONST VOID *Value, IN UINT32 Length)
{
    memset(mTree + mUsed, 0, DT_KEY_LEN + 4 + ((Length + 3) & ~3U));
    strcpy((CHAR8 *)mTree + mUsed, Key);
    memcpy(mTree + mUsed + DT_KEY_LEN, &Length, 4);
    memcpy(mTree + mUsed + DT_KEY_LEN + 4, Value, Length);
    mUsed += DT_KEY_LEN + 4 + ((Length + 3) & ~3U);
}

STATIC VOID Text(IN CONST CHAR8 *Key, IN CONST CHAR8 *Value)
{
    Prop(Key, Value, (UINT32)strlen(Value) + 1);
}

STATIC VOID Name(IN CONST CHAR8 *Value)
{
    Text("name", Value);
}

STATIC VOID Number(IN CONST CHAR8 *Key, IN UINT32 Value)
{
    Prop(Key, &Value, sizeof(Value));
}

//
// The tree a test has built, as the functions under test take it
//
STATIC CONST APPLE_BOOT_INFO *BuiltTree(VOID)
{
    STATIC struct boot_args mBootArgs;
    STATIC APPLE_BOOT_INFO  mInfo;

    mInfo.BootArgs       = &mBootArgs;
    mInfo.DeviceTree     = (dt_node_t *)mTree;
    mInfo.DeviceTreeSize = (UINT32)mUsed;
    return &mInfo;
}

//
// Checks
//
STATIC UINTN  mChecks;
STATIC UINTN  mFailures;

STATIC BOOLEAN Expect(IN BOOLEAN Ok, IN CONST CHAR8 *What)
{
    mChecks++;
    printf("%s  %s\n", Ok ? "ok  " : "FAIL", What);
    if(!Ok) {
        mFailures++;
    }
    return Ok;
}

STATIC int Verdict(VOID)
{
    if(mFailures != 0) {
        printf("%u of %u check(s) failed\n", (unsigned)mFailures, (unsigned)mChecks);
        return 1;
    }
    printf("all %u checks passed\n", (unsigned)mChecks);
    return 0;
}

//
// A machine, reduced to what the firmware reads from its tree:
//
// /device-tree              #address-cells 2, #size-cells 2
//   /chosen                 chip-id
//   /cpus                   #address-cells 1, #size-cells 0
//     /cpu<n> ...           cluster-type, cpu-id, state, device_type, reg
//     /not-a-core           a child without "cpu-id", which must not be taken
//   /arm-io                 #address-cells 2, #size-cells 2, ranges
//     /uart1                a "reg" that must not be taken
//     /uart0                reg
//
typedef struct
{
    UINT32      Number;         // "cpu-id"
    UINT32      Reg;
    CONST CHAR8 *Class;         // "cluster-type"; NULL leaves the property out
    CONST CHAR8 *State;         // NULL leaves the property out
} TEST_CPU;

typedef struct
{
    UINT32         ChipId;
    CONST TEST_CPU *Cpus;
    UINTN          CpuCount;
    CONST UINT64   (*Ranges)[3];    // address on the bus, address for the CPU, size
    UINTN          RangeCount;
    UINT64         UartReg[2];      // address on the bus, size
} TEST_MACHINE;

STATIC VOID BuildMachine(IN CONST TEST_MACHINE *Machine)
{
    STATIC CONST UINT64 OtherUart[2] = { 0x1ad204000ULL, 0x4000 };
    UINTN               Index;
    CHAR8               CpuName[16];

    mUsed = 0;
    Node(3, 3);
    Name("device-tree");
    Number("#address-cells", 2);
    Number("#size-cells", 2);

      Node(2, 0);
      Name("chosen");
      Number("chip-id", Machine->ChipId);

      Node(3, (UINT32)Machine->CpuCount + 1);
      Number("#address-cells", 1);
      Number("#size-cells", 0);
      Name("cpus");
      for(Index = 0; Index < Machine->CpuCount; Index++) {
          CONST TEST_CPU *Cpu = &Machine->Cpus[Index];

          Node(4 + (Cpu->Class != NULL ? 1 : 0) + (Cpu->State != NULL ? 1 : 0), 0);
          if(Cpu->Class != NULL) {
              Text("cluster-type", Cpu->Class);
          }
          Number("cpu-id", Cpu->Number);
          snprintf(CpuName, sizeof(CpuName), "cpu%u", (unsigned)Cpu->Number);
          Name(CpuName);
          if(Cpu->State != NULL) {
              Text("state", Cpu->State);
          }
          Text("device_type", "cpu");
          Number("reg", Cpu->Reg);
      }
        Node(2, 0);
        Name("not-a-core");
        Number("reg", 0x7ff);

      Node(4, 2);
      Number("#address-cells", 2);
      Name("arm-io");
      Prop("ranges", Machine->RangeCount != 0 ? (CONST VOID *)Machine->Ranges : "", (UINT32)(Machine->RangeCount * sizeof(Machine->Ranges[0])));
      Number("#size-cells", 2);
        Node(2, 0);
        Name("uart1");
        Prop("reg", OtherUart, sizeof(OtherUart));
        Node(2, 0);
        Prop("reg", Machine->UartReg, sizeof(Machine->UartReg));
        Name("uart0");
}

//
// The M4 MacBook Pro (Apple T8132), from its device tree as macOS shows it
// (SiliconNT docs/lab/2026-11-10-q3-macos-probes/ioreg-devicetree.log): ten
// cores, 0-5 efficiency cores, 6-9 performance cores, started on core 6; the 27
// "ranges" entries of /arm-io in the order of the tree; "reg" of uart0.
//
STATIC CONST TEST_CPU mCpusOfM4MacBookPro[] = {
    { 0, 0x000, "E", "waiting" },
    { 1, 0x001, "E", "waiting" },
    { 2, 0x002, "E", "waiting" },
    { 3, 0x003, "E", "waiting" },
    { 4, 0x004, "E", "waiting" },
    { 5, 0x005, "E", "waiting" },
    { 6, 0x100, "P", "running" },
    { 7, 0x101, "P", "waiting" },
    { 8, 0x102, "P", "waiting" },
    { 9, 0x103, "P", "waiting" },
};

STATIC CONST UINT64 mRangesOfM4MacBookPro[][3] = {
    { 0x0000000000ULL, 0x0200000000ULL, 0x3a0000000ULL },
    { 0x0400000000ULL, 0x0400000000ULL, 0x004074000ULL },
    { 0x0408000000ULL, 0x0408000000ULL, 0x004074000ULL },
    { 0x0410000000ULL, 0x0410000000ULL, 0x004074000ULL },
    { 0x0418000000ULL, 0x0418000000ULL, 0x004074000ULL },
    { 0x0800000000ULL, 0x0800000000ULL, 0x200000000ULL },
    { 0x0c00000000ULL, 0x0c00000000ULL, 0x200000000ULL },
    { 0x1000000000ULL, 0x1000000000ULL, 0x200000000ULL },
    { 0x1400000000ULL, 0x1400000000ULL, 0x200000000ULL },
    { 0x0a00000000ULL, 0x0a00000000ULL, 0x080000000ULL },
    { 0x0e00000000ULL, 0x0e00000000ULL, 0x080000000ULL },
    { 0x1200000000ULL, 0x1200000000ULL, 0x080000000ULL },
    { 0x1600000000ULL, 0x1600000000ULL, 0x080000000ULL },
    { 0x1c50000000ULL, 0x1c50000000ULL, 0x010000000ULL },
    { 0x1c60000000ULL, 0x1c60000000ULL, 0x010000000ULL },
    { 0x1c70000000ULL, 0x1c70000000ULL, 0x010000000ULL },
    { 0x1c80000000ULL, 0x1c80000000ULL, 0x010000000ULL },
    { 0x0404000000ULL, 0x0404000000ULL, 0x000074000ULL },
    { 0x040c000000ULL, 0x040c000000ULL, 0x000074000ULL },
    { 0x0414000000ULL, 0x0414000000ULL, 0x000074000ULL },
    { 0x041c000000ULL, 0x041c000000ULL, 0x000074000ULL },
    { 0x0490000000ULL, 0x0490000000ULL, 0x007060000ULL },
    { 0x1cb0000000ULL, 0x1cb0000000ULL, 0x010000000ULL },
    { 0x0bc0000000ULL, 0x0bc0000000ULL, 0x020000000ULL },
    { 0x0b80000000ULL, 0x0b80000000ULL, 0x040000000ULL },
    { 0x0ac0000000ULL, 0x0ac0000000ULL, 0x020000000ULL },
    { 0x0a80000000ULL, 0x0a80000000ULL, 0x040000000ULL },
};

STATIC CONST TEST_MACHINE mM4MacBookPro = {
    0x8132,
    mCpusOfM4MacBookPro, ARRAY_SIZE(mCpusOfM4MacBookPro),
    mRangesOfM4MacBookPro, ARRAY_SIZE(mRangesOfM4MacBookPro),
    { 0x1ad200000ULL, 0x4000 }
};

//
// The same machine as the host-side loader hands it over when it starts the
// firmware on one core: m1n1's hypervisor tooling removes the cores that are
// not running from the tree (proxyclient/m1n1/hv/__init__.py, "if not self.smp").
//
STATIC CONST TEST_MACHINE mM4MacBookProOneCore = {
    0x8132,
    &mCpusOfM4MacBookPro[6], 1,
    mRangesOfM4MacBookPro, ARRAY_SIZE(mRangesOfM4MacBookPro),
    { 0x1ad200000ULL, 0x4000 }
};

#endif // APPLE_DT_HOST_TEST_H_
