/** @file
  Host test of AppleGetBootRamdisk() in Library/AppleDTLib.

  Builds small Apple device trees in memory and checks what the function makes
  of the "RAMDisk" entry of /chosen/memory-map. Run it with ./run.sh; it needs
  the host compiler only.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "HostTest.h"

//
// Guest memory as the M4 had it in run R11: 0x100_17b1_8000 .. 0x103_db61_0000
//
#define MEM_BASE  0x10017b18000ULL
#define MEM_TOP   0x103db610000ULL

//
// /device-tree
//   /arm-io                 has a "RAMDisk" property of its own, which must not be taken
//   /chosen
//     /memory-map-old       same
//     /memory-map           DeviceTree, BootArgs and, if Entry is not NULL, RAMDisk
//
STATIC VOID BuildTree(IN BOOLEAN WithMemoryMap, IN CONST VOID *Entry, IN UINT32 EntryLength)
{
    STATIC CONST UINT8  Decoy[16]  = { 0, 0, 0, 0x20, 0x00, 0x01, 0, 0, 0, 0, 0x10, 0, 0, 0, 0, 0 };
    STATIC CONST UINT64 Other[2]   = { MEM_BASE, 0x4000 };

    mUsed = 0;
    Node(1, 2);
    Name("device-tree");
      Node(2, 0);
      Name("arm-io");
      Prop("RAMDisk", Decoy, sizeof(Decoy));
      Node(1, WithMemoryMap ? 2 : 1);
      Name("chosen");
        Node(2, 0);
        Name("memory-map-old");
        Prop("RAMDisk", Decoy, sizeof(Decoy));
        if(WithMemoryMap) {
            Node(Entry != NULL ? 4 : 3, 0);
            Name("memory-map");
            Prop("DeviceTree", Other, sizeof(Other));
            if(Entry != NULL) {
                Prop("RAMDisk", Entry, EntryLength);
            }
            Prop("BootArgs", Other, sizeof(Other));
        }
}

STATIC VOID Check(IN CONST CHAR8 *What, IN BOOLEAN WithMemoryMap, IN CONST VOID *Entry, IN UINT32 EntryLength, IN RETURN_STATUS Expected, IN UINT64 ExpectedBase, IN UINT64 ExpectedSize)
{
    struct boot_args BootArgs;
    APPLE_BOOT_INFO  Info;
    RETURN_STATUS    Status;
    UINT64           Base = 0x1111;
    UINT64           Size = 0x2222;
    BOOLEAN          Ok;

    BuildTree(WithMemoryMap, Entry, EntryLength);
    if(dt_check(mTree, mUsed, NULL) != 0) {
        Expect(FALSE, What);
        printf("      the test built a tree dt_check() rejects\n");
        return;
    }

    memset(&BootArgs, 0, sizeof(BootArgs));
    BootArgs.phys_base  = MEM_BASE;
    BootArgs.mem_size   = MEM_TOP - MEM_BASE;
    Info.BootArgs       = &BootArgs;
    Info.DeviceTree     = (dt_node_t *)mTree;
    Info.DeviceTreeSize = (UINT32)mUsed;

    Status = AppleGetBootRamdisk(&Info, &Base, &Size);
    Ok     = Status == Expected;
    if(Ok && Status == RETURN_SUCCESS) {
        Ok = Base == ExpectedBase && Size == ExpectedSize;
    }
    if(!Expect(Ok, What)) {
        printf("      status 0x%llx base 0x%llx size 0x%llx\n", (unsigned long long)Status, (unsigned long long)Base, (unsigned long long)Size);
    }
}

#define PAIR(Base, Size)  (CONST UINT64[]){ (Base), (Size) }, 16

int main(void)
{
    // base 0x100_2000_0000, size 0x2000_0000, byte by byte: the encoding is little-endian
    STATIC CONST UINT8  Bytes[16] = { 0, 0, 0, 0x20, 0x00, 0x01, 0, 0, 0, 0, 0, 0x20, 0, 0, 0, 0 };
    STATIC CONST UINT64 Three[3]  = { 0x10020000000ULL, 0x20000000ULL, 0 };

    Check("no RAMDisk entry", TRUE, NULL, 0, RETURN_NOT_FOUND, 0, 0);
    Check("no /chosen/memory-map", FALSE, NULL, 0, RETURN_NOT_FOUND, 0, 0);

    Check("512 MB above 4 GB", TRUE, Bytes, sizeof(Bytes), RETURN_SUCCESS, 0x10020000000ULL, 0x20000000ULL);
    Check("1 GB", TRUE, PAIR(0x10020000000ULL, 0x40000000ULL), RETURN_SUCCESS, 0x10020000000ULL, 0x40000000ULL);
    Check("one page at the start of memory", TRUE, PAIR(MEM_BASE, 0x1000), RETURN_SUCCESS, MEM_BASE, 0x1000);
    Check("ends exactly at the top of memory", TRUE, PAIR(MEM_TOP - 0x200000, 0x200000), RETURN_SUCCESS, MEM_TOP - 0x200000, 0x200000);
    Check("all of memory", TRUE, PAIR(MEM_BASE, MEM_TOP - MEM_BASE), RETURN_SUCCESS, MEM_BASE, MEM_TOP - MEM_BASE);

    Check("entry of 8 bytes", TRUE, Bytes, 8, RETURN_INVALID_PARAMETER, 0, 0);
    Check("entry of 24 bytes", TRUE, Three, sizeof(Three), RETURN_INVALID_PARAMETER, 0, 0);
    Check("empty entry", TRUE, Bytes, 0, RETURN_INVALID_PARAMETER, 0, 0);
    Check("size 0", TRUE, PAIR(0x10020000000ULL, 0), RETURN_INVALID_PARAMETER, 0, 0);
    Check("base and size 0", TRUE, PAIR(0, 0), RETURN_INVALID_PARAMETER, 0, 0);
    Check("base not on a page boundary", TRUE, PAIR(0x10020000200ULL, 0x20000000ULL), RETURN_INVALID_PARAMETER, 0, 0);
    Check("size not a multiple of the page size", TRUE, PAIR(0x10020000000ULL, 0x20000200ULL), RETURN_INVALID_PARAMETER, 0, 0);
    Check("starts below memory", TRUE, PAIR(MEM_BASE - 0x1000, 0x2000), RETURN_INVALID_PARAMETER, 0, 0);
    Check("ends one page above memory", TRUE, PAIR(MEM_TOP - 0x1000, 0x2000), RETURN_INVALID_PARAMETER, 0, 0);
    Check("starts at the top of memory", TRUE, PAIR(MEM_TOP, 0x1000), RETURN_INVALID_PARAMETER, 0, 0);
    Check("starts above memory", TRUE, PAIR(MEM_TOP + 0x100000, 0x1000), RETURN_INVALID_PARAMETER, 0, 0);
    Check("below 4 GB", TRUE, PAIR(0x80000000ULL, 0x20000000ULL), RETURN_INVALID_PARAMETER, 0, 0);
    Check("base + size wraps around", TRUE, PAIR(0x10020000000ULL, 0xfffffffffffff000ULL), RETURN_INVALID_PARAMETER, 0, 0);
    Check("base at the end of the address space", TRUE, PAIR(0xfffffffffffff000ULL, 0x2000), RETURN_INVALID_PARAMETER, 0, 0);

    return Verdict();
}
