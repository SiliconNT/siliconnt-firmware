/** @file
  Host test of AppleGetAicToGic() and AppleFindGicInterrupt() in Library/AppleDTLib.

  Builds small Apple device trees in memory, puts the table of interrupt numbers
  into a stand-in for guest memory, and checks what the functions make of them.
  Run it with ./run.sh; it needs the host compiler only.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "HostTest.h"

//
// Guest memory, four pages. The table goes to the start of the second, as the
// loader puts it at the start of a page; the third holds a table only decoys
// point to.
//
STATIC UINT8 mMemory[4 * EFI_PAGE_SIZE] __attribute__((aligned(EFI_PAGE_SIZE)));

#define MEM_BASE    ((UINT64)(UINTN)mMemory)
#define MEM_TOP     (MEM_BASE + sizeof(mMemory))
#define TABLE_BASE  (MEM_BASE + EFI_PAGE_SIZE)
#define DECOY_BASE  (MEM_BASE + 2 * EFI_PAGE_SIZE)

//
// Writes a table at Base: Version, Count, then Written numbers from Aic.
// Returns its size in bytes, 8 + 4 * Written.
//
STATIC UINT64 WriteTable(IN UINT64 Base, IN UINT32 Version, IN UINT32 Count, IN CONST UINT32 *Aic, IN UINT32 Written)
{
    UINT32 *Words = (UINT32 *)(UINTN)Base;

    Words[0] = Version;
    Words[1] = Count;
    memcpy(&Words[2], Aic, Written * sizeof(UINT32));
    return 8 + 4 * (UINT64)Written;
}

//
// /device-tree
//   /arm-io                 has an "AICToGIC" property of its own, which must not be taken
//   /chosen
//     /memory-map-old       same
//     /memory-map           DeviceTree, BootArgs and, if Entry is not NULL, AICToGIC
//
// The decoys name a good table of one number, so that taking one shows.
//
STATIC VOID BuildTree(IN BOOLEAN WithMemoryMap, IN CONST VOID *Entry, IN UINT32 EntryLength)
{
    STATIC CONST UINT32 DecoyNumbers[1] = { 7 };
    UINT64              Decoy[2];
    STATIC CONST UINT64 Other[2]        = { 0x10017b18000ULL, 0x4000 };

    Decoy[0] = DECOY_BASE;
    Decoy[1] = WriteTable(DECOY_BASE, 1, 1, DecoyNumbers, 1);

    mUsed = 0;
    Node(1, 2);
    Name("device-tree");
      Node(2, 0);
      Name("arm-io");
      Prop("AICToGIC", Decoy, sizeof(Decoy));
      Node(1, WithMemoryMap ? 2 : 1);
      Name("chosen");
        Node(2, 0);
        Name("memory-map-old");
        Prop("AICToGIC", Decoy, sizeof(Decoy));
        if(WithMemoryMap) {
            Node(Entry != NULL ? 4 : 3, 0);
            Name("memory-map");
            Prop("DeviceTree", Other, sizeof(Other));
            if(Entry != NULL) {
                Prop("AICToGIC", Entry, EntryLength);
            }
            Prop("BootArgs", Other, sizeof(Other));
        }
}

//
// Reads the table the tree names. Checks the status and, on RETURN_SUCCESS, that
// the numbers handed out are those 8 bytes into the table and there are
// ExpectedCount of them; on any other status, that no numbers are handed out.
//
STATIC BOOLEAN CheckRead(IN CONST CHAR8 *What, IN BOOLEAN WithMemoryMap, IN CONST VOID *Entry, IN UINT32 EntryLength, IN RETURN_STATUS Expected, IN UINT32 ExpectedCount, OUT APPLE_AIC_TO_GIC *Table)
{
    struct boot_args BootArgs;
    APPLE_BOOT_INFO  Info;
    RETURN_STATUS    Status;
    BOOLEAN          Ok;

    BuildTree(WithMemoryMap, Entry, EntryLength);
    if(dt_check(mTree, mUsed, NULL) != 0) {
        Expect(FALSE, What);
        printf("      the test built a tree dt_check() rejects\n");
        return FALSE;
    }

    memset(&BootArgs, 0, sizeof(BootArgs));
    BootArgs.phys_base  = MEM_BASE;
    BootArgs.mem_size   = MEM_TOP - MEM_BASE;
    Info.BootArgs       = &BootArgs;
    Info.DeviceTree     = (dt_node_t *)mTree;
    Info.DeviceTreeSize = (UINT32)mUsed;
    Info.UartBase       = 0;

    memset(Table, 0x5a, sizeof(*Table));
    Status = AppleGetAicToGic(&Info, Table);
    Ok     = Status == Expected;
    if(Ok && Status == RETURN_SUCCESS) {
        Ok = Table->Aic == (CONST UINT32 *)(UINTN)(Table->Base + 8) && Table->Count == ExpectedCount;
    } else if(Ok) {
        Ok = Table->Aic == NULL;
    }
    if(!Expect(Ok, What)) {
        printf(
            "      status 0x%llx base 0x%llx size 0x%llx version %u count %u numbers %p\n",
            (unsigned long long)Status, (unsigned long long)Table->Base, (unsigned long long)Table->Size,
            (unsigned)Table->Version, (unsigned)Table->Count, (CONST VOID *)Table->Aic);
    }
    return Ok;
}

STATIC VOID CheckFind(IN CONST CHAR8 *What, IN CONST APPLE_AIC_TO_GIC *Table, IN UINT32 Aic, IN RETURN_STATUS Expected, IN UINT32 ExpectedGic)
{
    UINT32        Gic    = 0x5a5a;
    RETURN_STATUS Status = AppleFindGicInterrupt(Table, Aic, &Gic);

    if(!Expect(Status == Expected && Gic == ExpectedGic, What)) {
        printf("      AIC %u: status 0x%llx GIC %u\n", (unsigned)Aic, (unsigned long long)Status, (unsigned)Gic);
    }
}

#define ENTRY(Base, Size)  (CONST UINT64[]){ (Base), (Size) }, 16

int main(void)
{
    //
    // Numbers the M4 MacBook Pro has in its tree (SiliconNT findings/0058,
    // evidence section 1): the lowest, 0, shared by three nodes; 678, 750 and 824,
    // each shared by a display controller and its DART; the highest, 1505.
    //
    STATIC CONST UINT32 Numbers[]        = { 0, 5, 678, 750, 824, 1505 };
    STATIC CONST UINT32 Descending[]     = { 0, 5, 750, 678, 824, 1505 };
    STATIC CONST UINT32 Duplicate[]      = { 0, 5, 678, 678, 824, 1505 };
    STATIC CONST UINT32 LastOutOfOrder[] = { 0, 5, 678, 750, 824, 823 };
    STATIC UINT32       Full[APPLE_AIC_TO_GIC_MAX_COUNT + 1];
    UINT8               Bytes[16];
    UINT64              Size;
    UINT32              Index;
    UINT32              Gic;
    BOOLEAN             AllFound;
    APPLE_AIC_TO_GIC    Table;
    APPLE_AIC_TO_GIC    Empty;

    for(Index = 0; Index < ARRAY_SIZE(Full); Index++) {
        Full[Index] = 3 * Index + 1;    // 1, 4, ..., 2965: above the largest a die has
    }

    //
    // No table
    //
    BuildMachine(&mM4MacBookPro);
    Expect(AppleGetAicToGic(BuiltTree(), &Table) == RETURN_NOT_FOUND && Table.Aic == NULL, "the tree of the M4 MacBook Pro, which has no /chosen/memory-map: no table");
    CheckRead("no AICToGIC entry", TRUE, NULL, 0, RETURN_NOT_FOUND, 0, &Table);
    CheckRead("no /chosen/memory-map", FALSE, NULL, 0, RETURN_NOT_FOUND, 0, &Table);

    //
    // Good tables
    //
    Size = WriteTable(TABLE_BASE, 1, ARRAY_SIZE(Numbers), Numbers, ARRAY_SIZE(Numbers));
    if(CheckRead("six numbers", TRUE, ENTRY(TABLE_BASE, Size), RETURN_SUCCESS, ARRAY_SIZE(Numbers), &Table)) {
        CheckFind("the lowest number, 0, is GIC 32", &Table, 0, RETURN_SUCCESS, 32);
        CheckFind("5 is GIC 33", &Table, 5, RETURN_SUCCESS, 33);
        CheckFind("678 is GIC 34", &Table, 678, RETURN_SUCCESS, 34);
        CheckFind("750 is GIC 35", &Table, 750, RETURN_SUCCESS, 35);
        CheckFind("824 is GIC 36", &Table, 824, RETURN_SUCCESS, 36);
        CheckFind("the highest number, 1505, is GIC 37", &Table, 1505, RETURN_SUCCESS, 37);
        CheckFind("1 is not there", &Table, 1, RETURN_NOT_FOUND, 0);
        CheckFind("700, between two numbers, is not there", &Table, 700, RETURN_NOT_FOUND, 0);
        CheckFind("1506, above the highest, is not there", &Table, 1506, RETURN_NOT_FOUND, 0);
        CheckFind("0xffffffff is not there", &Table, 0xffffffffU, RETURN_NOT_FOUND, 0);
    }

    // The entry byte by byte: the encoding is little-endian
    Size = WriteTable(TABLE_BASE, 1, ARRAY_SIZE(Numbers), Numbers, ARRAY_SIZE(Numbers));
    for(Index = 0; Index < 8; Index++) {
        Bytes[Index]     = (UINT8)(TABLE_BASE >> (8 * Index));
        Bytes[8 + Index] = (UINT8)(Size >> (8 * Index));
    }
    CheckRead("the entry written byte by byte", TRUE, Bytes, sizeof(Bytes), RETURN_SUCCESS, ARRAY_SIZE(Numbers), &Table);

    Size = WriteTable(TABLE_BASE, 1, APPLE_AIC_TO_GIC_MAX_COUNT, Full, APPLE_AIC_TO_GIC_MAX_COUNT);
    if(CheckRead("988 numbers, as many as there are SPIs", TRUE, ENTRY(TABLE_BASE, Size), RETURN_SUCCESS, APPLE_AIC_TO_GIC_MAX_COUNT, &Table)) {
        CheckFind("the first of 988 is GIC 32", &Table, Full[0], RETURN_SUCCESS, 32);
        CheckFind("the last of 988 is GIC 1019", &Table, Full[APPLE_AIC_TO_GIC_MAX_COUNT - 1], RETURN_SUCCESS, 1019);
        AllFound = TRUE;
        for(Index = 0; Index < APPLE_AIC_TO_GIC_MAX_COUNT; Index++) {
            AllFound = AllFound && AppleFindGicInterrupt(&Table, Full[Index], &Gic) == RETURN_SUCCESS && Gic == 32 + Index;
            AllFound = AllFound && AppleFindGicInterrupt(&Table, Full[Index] + 1, &Gic) == RETURN_NOT_FOUND && Gic == 0;
        }
        Expect(AllFound, "each of 988 is GIC 32 + its index; the number after each is not there");
    }

    Size = WriteTable(TABLE_BASE, 1, 0, Numbers, 0);
    if(CheckRead("no numbers", TRUE, ENTRY(TABLE_BASE, Size), RETURN_SUCCESS, 0, &Table)) {
        CheckFind("nothing is in a table of no numbers", &Table, 0, RETURN_NOT_FOUND, 0);
    }

    Size = WriteTable(MEM_TOP - EFI_PAGE_SIZE, 1, 1, Numbers, 1);
    CheckRead("one number in the last page of memory", TRUE, ENTRY(MEM_TOP - EFI_PAGE_SIZE, Size), RETURN_SUCCESS, 1, &Table);

    //
    // An entry that cannot be used: the table is not read
    //
    Size = WriteTable(TABLE_BASE, 1, ARRAY_SIZE(Numbers), Numbers, ARRAY_SIZE(Numbers));
    CheckRead("entry of 8 bytes", TRUE, (CONST UINT64[]){ TABLE_BASE }, 8, RETURN_INVALID_PARAMETER, 0, &Table);
    CheckRead("entry of 24 bytes", TRUE, (CONST UINT64[]){ TABLE_BASE, Size, 0 }, 24, RETURN_INVALID_PARAMETER, 0, &Table);
    CheckRead("empty entry", TRUE, "", 0, RETURN_INVALID_PARAMETER, 0, &Table);
    CheckRead("size 0", TRUE, ENTRY(TABLE_BASE, 0), RETURN_INVALID_PARAMETER, 0, &Table);
    CheckRead("size 4, no room for the count", TRUE, ENTRY(TABLE_BASE, 4), RETURN_INVALID_PARAMETER, 0, &Table);
    Expect(Table.Version == 0 && Table.Count == 0, "size 4: the header is not read");
    CheckRead("base 0 and size 0", TRUE, ENTRY(0, 0), RETURN_INVALID_PARAMETER, 0, &Table);
    WriteTable(TABLE_BASE + 4, 1, ARRAY_SIZE(Numbers), Numbers, ARRAY_SIZE(Numbers));
    CheckRead("base not the start of a page", TRUE, ENTRY(TABLE_BASE + 4, Size), RETURN_INVALID_PARAMETER, 0, &Table);
    CheckRead("starts below memory", TRUE, ENTRY(MEM_BASE - EFI_PAGE_SIZE, Size), RETURN_INVALID_PARAMETER, 0, &Table);
    CheckRead("starts at the top of memory", TRUE, ENTRY(MEM_TOP, Size), RETURN_INVALID_PARAMETER, 0, &Table);
    CheckRead("ends above memory", TRUE, ENTRY(MEM_TOP - EFI_PAGE_SIZE, EFI_PAGE_SIZE + 8), RETURN_INVALID_PARAMETER, 0, &Table);
    CheckRead("base + size wraps around", TRUE, ENTRY(TABLE_BASE, 0xfffffffffffff000ULL), RETURN_INVALID_PARAMETER, 0, &Table);
    CheckRead("base at the end of the address space", TRUE, ENTRY(0xfffffffffffff000ULL, Size), RETURN_INVALID_PARAMETER, 0, &Table);

    //
    // A table that cannot be used: it is read, the numbers are not handed out
    //
    Size = WriteTable(TABLE_BASE, 2, ARRAY_SIZE(Numbers), Numbers, ARRAY_SIZE(Numbers));
    CheckRead("version 2", TRUE, ENTRY(TABLE_BASE, Size), RETURN_INCOMPATIBLE_VERSION, 0, &Table);
    Expect(Table.Version == 2 && Table.Count == ARRAY_SIZE(Numbers), "version 2: what the header says is handed back");
    Size = WriteTable(TABLE_BASE, 0, ARRAY_SIZE(Numbers), Numbers, ARRAY_SIZE(Numbers));
    CheckRead("version 0", TRUE, ENTRY(TABLE_BASE, Size), RETURN_INCOMPATIBLE_VERSION, 0, &Table);

    Size = WriteTable(TABLE_BASE, 1, APPLE_AIC_TO_GIC_MAX_COUNT + 1, Full, APPLE_AIC_TO_GIC_MAX_COUNT + 1);
    CheckRead("989 numbers, one more than there are SPIs", TRUE, ENTRY(TABLE_BASE, Size), RETURN_BAD_BUFFER_SIZE, 0, &Table);
    Size = WriteTable(TABLE_BASE, 1, 0xffffffffU, Numbers, ARRAY_SIZE(Numbers));
    CheckRead("count 0xffffffff", TRUE, ENTRY(TABLE_BASE, Size), RETURN_BAD_BUFFER_SIZE, 0, &Table);
    Size = WriteTable(TABLE_BASE, 1, ARRAY_SIZE(Numbers), Numbers, ARRAY_SIZE(Numbers));
    CheckRead("size 4 bytes short of the count", TRUE, ENTRY(TABLE_BASE, Size - 4), RETURN_BAD_BUFFER_SIZE, 0, &Table);
    CheckRead("size 4 bytes over the count", TRUE, ENTRY(TABLE_BASE, Size + 4), RETURN_BAD_BUFFER_SIZE, 0, &Table);
    CheckRead("size of a whole page", TRUE, ENTRY(TABLE_BASE, EFI_PAGE_SIZE), RETURN_BAD_BUFFER_SIZE, 0, &Table);
    Size = WriteTable(TABLE_BASE, 1, ARRAY_SIZE(Numbers) - 1, Numbers, ARRAY_SIZE(Numbers));
    CheckRead("count one below the numbers the size holds", TRUE, ENTRY(TABLE_BASE, Size), RETURN_BAD_BUFFER_SIZE, 0, &Table);

    Size = WriteTable(TABLE_BASE, 1, ARRAY_SIZE(Descending), Descending, ARRAY_SIZE(Descending));
    CheckRead("two numbers the wrong way round", TRUE, ENTRY(TABLE_BASE, Size), RETURN_COMPROMISED_DATA, 0, &Table);
    Size = WriteTable(TABLE_BASE, 1, ARRAY_SIZE(Duplicate), Duplicate, ARRAY_SIZE(Duplicate));
    CheckRead("a number twice", TRUE, ENTRY(TABLE_BASE, Size), RETURN_COMPROMISED_DATA, 0, &Table);
    Size = WriteTable(TABLE_BASE, 1, ARRAY_SIZE(LastOutOfOrder), LastOutOfOrder, ARRAY_SIZE(LastOutOfOrder));
    CheckRead("the last number below the one before", TRUE, ENTRY(TABLE_BASE, Size), RETURN_COMPROMISED_DATA, 0, &Table);

    //
    // A table that was not read holds nothing
    //
    memset(&Empty, 0, sizeof(Empty));
    Empty.Count = 3;
    CheckFind("a table with no numbers handed out holds nothing", &Empty, 0, RETURN_NOT_FOUND, 0);

    return Verdict();
}
