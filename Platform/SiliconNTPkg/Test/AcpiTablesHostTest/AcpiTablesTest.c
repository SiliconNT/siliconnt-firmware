/** @file
  Host test of the ACPI tables the firmware builds at boot (MADT, DBG2, DSDT):
  from a device tree the test builds, through Library/AppleDTLib, to the bytes
  Drivers/AcpiPlatformDxe/AcpiTableBuilder.c produces.

  For the tree of the M4 MacBook Pro the tables must be the ones the firmware had
  as static tables before (commit 816ea14, kept in Static816ea14/), byte for
  byte. Other trees are checked field by field. Every DSDT is also written out
  next to the ASL that describes it, for run.sh to compare with what the ASL
  compiler makes of that ASL.

  Usage: AcpiTablesTest <directory of the static tables> <directory to write to>.
  Run it with ./run.sh.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "../../../../Silicon/Apple/AppleSiliconPkg/Test/AppleDTLibHostTest/HostTest.h"

#include "../../Drivers/AcpiPlatformDxe/AcpiTableBuilder.c"

#include <stdlib.h>

//
// What the EL2 layer emulates and what the platform description file sets
//
#define DISTRIBUTOR_BASE     0x5000000000ULL
#define REDISTRIBUTORS_BASE  0x5100000000ULL
#define CREATOR_REVISION     1

STATIC CONST CHAR8 *mStaticDirectory;
STATIC CONST CHAR8 *mOutputDirectory;

STATIC UINT8 mTable[4096];

//
// A made-up machine with another shape: eight cores, two efficiency cores and
// two clusters of three performance cores, started on core 2
//
STATIC CONST TEST_CPU mOtherCpus[] = {
    { 0, 0x000, "E", "waiting" },
    { 1, 0x001, "E", "waiting" },
    { 2, 0x100, "P", "running" },
    { 3, 0x101, "P", "waiting" },
    { 4, 0x102, "P", "waiting" },
    { 5, 0x200, "P", "waiting" },
    { 6, 0x201, "P", "waiting" },
    { 7, 0x202, "P", "waiting" },
};

STATIC CONST UINT64 mOtherRanges[][3] = {
    { 0x000000000ULL, 0x280000000ULL, 0x100000000ULL },
};

STATIC CONST TEST_MACHINE mOther = {
    0x6000,
    mOtherCpus, ARRAY_SIZE(mOtherCpus),
    mOtherRanges, ARRAY_SIZE(mOtherRanges),
    { 0x039200000ULL, 0x4000 }
};

//
// Core numbers with gaps, in no order, started on the one in the middle
//
STATIC CONST TEST_CPU mSparseCpus[] = {
    { 9, 0x101, "P", "waiting" },
    { 3, 0x003, "E", "running" },
    { 1, 0x001, "E", "waiting" },
};

STATIC CONST TEST_MACHINE mSparse = {
    0x8132,
    mSparseCpus, ARRAY_SIZE(mSparseCpus),
    mOtherRanges, ARRAY_SIZE(mOtherRanges),
    { 0x039200000ULL, 0x4000 }
};

typedef struct
{
    UINT32 Number;
    UINT64 Mpidr;
    UINT8  Efficiency;
} EXPECTED_GICC;

STATIC UINTN ReadStatic(IN CONST CHAR8 *FileName, OUT UINT8 *Buffer, IN UINTN Capacity)
{
    CHAR8  Path[1024];
    FILE   *File;
    UINTN  Size;

    snprintf(Path, sizeof(Path), "%s/%s", mStaticDirectory, FileName);
    File = fopen(Path, "rb");
    if(File == NULL) {
        printf("cannot open %s\n", Path);
        exit(2);
    }
    Size = fread(Buffer, 1, Capacity, File);
    fclose(File);
    return Size;
}

STATIC VOID WriteOutput(IN CONST CHAR8 *Name, IN CONST CHAR8 *Extension, IN CONST VOID *Data, IN UINTN Size)
{
    CHAR8 Path[1024];
    FILE  *File;

    snprintf(Path, sizeof(Path), "%s/%s.%s", mOutputDirectory, Name, Extension);
    File = fopen(Path, "wb");
    if(File == NULL || fwrite(Data, 1, Size, File) != Size || fclose(File) != 0) {
        printf("cannot write %s\n", Path);
        exit(2);
    }
}

// What the firmware reads from the tree of Machine, as AcpiPlatformDxe does
STATIC BOOLEAN ReadMachine(IN CONST TEST_MACHINE *Machine, OUT APPLE_CPU *Cpus, OUT UINTN *Count, OUT UINT64 *UartBase, OUT UINT32 *ChipId)
{
    BuildMachine(Machine);
    return dt_check(mTree, mUsed, NULL) == 0 &&
           AppleGetCpus(BuiltTree(), Cpus, Count) == RETURN_SUCCESS &&
           AppleGetUartBase(BuiltTree(), UartBase) == RETURN_SUCCESS &&
           AppleGetChipId(BuiltTree(), ChipId) == RETURN_SUCCESS;
}

// The fields every table header of the firmware has in common
STATIC BOOLEAN HeaderIs(IN CONST EFI_ACPI_DESCRIPTION_HEADER *Header, IN CONST CHAR8 *Signature, IN UINTN Length, IN UINT8 Revision, IN CONST CHAR8 *OemId, IN UINT32 ChipId)
{
    return memcmp(&Header->Signature, Signature, 4) == 0 && Header->Length == Length && Header->Revision == Revision &&
           Header->Checksum == 0 && memcmp(Header->OemId, OemId, 6) == 0 && memcmp(&Header->OemTableId, "APPLEEFI", 8) == 0 &&
           Header->OemRevision == ChipId && memcmp(&Header->CreatorId, "APPL", 4) == 0 && Header->CreatorRevision == CREATOR_REVISION;
}

STATIC VOID CheckMadt(IN CONST CHAR8 *What, IN CONST TEST_MACHINE *Machine, IN CONST EXPECTED_GICC *Expected, IN UINTN ExpectedCount, IN UINTN ExpectedRedistributors)
{
    APPLE_CPU                                            Cpus[APPLE_MAX_CPUS];
    UINTN                                                Count;
    UINT64                                               UartBase;
    UINT32                                               ChipId;
    EFI_ACPI_6_4_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER  *Header = (VOID *)mTable;
    EFI_ACPI_6_4_GIC_STRUCTURE                           *Gicc   = (VOID *)(Header + 1);
    EFI_ACPI_6_4_GIC_DISTRIBUTOR_STRUCTURE               *Gicd;
    EFI_ACPI_6_4_GICR_STRUCTURE                          *Gicr;
    UINTN                                                Size    = 44 + 80 * ExpectedCount + 24 + 16;
    UINTN                                                Index;
    BOOLEAN                                              Ok;
    CHAR8                                                Line[256];

    memset(mTable, 0xa5, sizeof(mTable));
    Ok = ReadMachine(Machine, Cpus, &Count, &UartBase, &ChipId) && Count == ExpectedCount && MadtSize(Count) == Size &&
         MadtBuild(ChipId, CREATOR_REVISION, Cpus, Count, DISTRIBUTOR_BASE, REDISTRIBUTORS_BASE, mTable) == RETURN_SUCCESS;
    snprintf(Line, sizeof(Line), "%s: the MADT is built, %u bytes", What, (unsigned)Size);
    if(!Expect(Ok, Line)) {
        return;
    }

    snprintf(Line, sizeof(Line), "%s: nothing is written beyond the table", What);
    Expect(mTable[Size] == 0xa5 && mTable[sizeof(mTable) - 1] == 0xa5, Line);

    snprintf(Line, sizeof(Line), "%s: header, revision 5, OEM revision is the chip number 0x%x", What, (unsigned)Machine->ChipId);
    Expect(HeaderIs(&Header->Header, "APIC", Size, 5, "Apple\0", Machine->ChipId) && Header->LocalApicAddress == 0 && Header->Flags == 0, Line);

    for(Index = 0; Index < ExpectedCount; Index++) {
        Ok = Gicc[Index].Type == 0x0B && Gicc[Index].Length == 80 && Gicc[Index].Reserved == 0 &&
             Gicc[Index].CPUInterfaceNumber == Expected[Index].Number && Gicc[Index].AcpiProcessorUid == Expected[Index].Number &&
             Gicc[Index].Flags == 1 && Gicc[Index].ParkingProtocolVersion == 0 && Gicc[Index].PerformanceInterruptGsiv == 0 &&
             Gicc[Index].ParkedAddress == 0 && Gicc[Index].PhysicalBaseAddress == 0 && Gicc[Index].GICV == 0 && Gicc[Index].GICH == 0 &&
             Gicc[Index].VGICMaintenanceInterrupt == 0 && Gicc[Index].GICRBaseAddress == 0 &&
             Gicc[Index].MPIDR == Expected[Index].Mpidr && Gicc[Index].ProcessorPowerEfficiencyClass == Expected[Index].Efficiency &&
             Gicc[Index].Reserved2 == 0 && Gicc[Index].SpeOverflowInterrupt == 0;
        snprintf(Line, sizeof(Line), "%s: GICC %u is core %u, MPIDR 0x%llx, efficiency class %u, enabled%s", What, (unsigned)Index,
                 (unsigned)Expected[Index].Number, (unsigned long long)Expected[Index].Mpidr, (unsigned)Expected[Index].Efficiency, Index == 0 ? ", the boot core" : "");
        if(!Expect(Ok, Line)) {
            printf("      core %u, UID %u, MPIDR 0x%llx, efficiency class %u, flags 0x%x\n", (unsigned)Gicc[Index].CPUInterfaceNumber, (unsigned)Gicc[Index].AcpiProcessorUid,
                   (unsigned long long)Gicc[Index].MPIDR, (unsigned)Gicc[Index].ProcessorPowerEfficiencyClass, (unsigned)Gicc[Index].Flags);
        }
    }

    Gicd = (VOID *)&Gicc[ExpectedCount];
    snprintf(Line, sizeof(Line), "%s: Distributor at 0x%llx, version 3", What, DISTRIBUTOR_BASE);
    Expect(Gicd->Type == 0x0C && Gicd->Length == 24 && Gicd->Reserved1 == 0 && Gicd->GicId == 0 && Gicd->PhysicalBaseAddress == DISTRIBUTOR_BASE &&
           Gicd->SystemVectorBase == 0 && Gicd->GicVersion == 3 && Gicd->Reserved2[0] == 0 && Gicd->Reserved2[1] == 0 && Gicd->Reserved2[2] == 0, Line);

    Gicr = (VOID *)(Gicd + 1);
    snprintf(Line, sizeof(Line), "%s: %u Redistributors from 0x%llx", What, (unsigned)ExpectedRedistributors, REDISTRIBUTORS_BASE);
    if(!Expect(Gicr->Type == 0x0E && Gicr->Length == 16 && Gicr->Reserved == 0 && Gicr->DiscoveryRangeBaseAddress == REDISTRIBUTORS_BASE &&
               Gicr->DiscoveryRangeLength == 0x20000 * ExpectedRedistributors && EmulatedGicRedistributorCount(Cpus, Count) == ExpectedRedistributors, Line)) {
        printf("      base 0x%llx length 0x%x\n", (unsigned long long)Gicr->DiscoveryRangeBaseAddress, (unsigned)Gicr->DiscoveryRangeLength);
    }
}

STATIC VOID CheckDbg2(IN CONST CHAR8 *What, IN CONST TEST_MACHINE *Machine, IN UINT64 ExpectedUart)
{
    APPLE_CPU  Cpus[APPLE_MAX_CPUS];
    UINTN      Count;
    UINT64     UartBase;
    UINT32     ChipId;
    UINT64     Address;
    UINT32     AddressSize;
    CHAR8      Line[256];

    memset(mTable, 0xa5, sizeof(mTable));
    if(!ReadMachine(Machine, Cpus, &Count, &UartBase, &ChipId) || Dbg2Size() != 84) {
        snprintf(Line, sizeof(Line), "%s: the tree is read, the DBG2 has 84 bytes", What);
        Expect(FALSE, Line);
        return;
    }
    Dbg2Build(ChipId, CREATOR_REVISION, UartBase, mTable);
    snprintf(Line, sizeof(Line), "%s: DBG2 of 84 bytes, one Samsung-style serial port at 0x%llx, nothing written beyond it", What, (unsigned long long)ExpectedUart);

    //
    // By offset, not through the structures of the builder: table header 36,
    // offset and number of the device entries 8; then the entry: its header 22,
    // the register 12 (space 0 = memory, width 32, offset 0, access 3 = 32 bits,
    // address), the address size 4, the name ".".
    //
    memcpy(&Address, mTable + 44 + 22 + 4, 8);
    memcpy(&AddressSize, mTable + 44 + 22 + 12, 4);
    Expect(HeaderIs((VOID *)mTable, "DBG2", 84, 0, "Apple\0", Machine->ChipId) &&
           memcmp(mTable + 36, "\x2c\0\0\0\x01\0\0\0", 8) == 0 &&
           memcmp(mTable + 44, "\0\x28\0\x01\x02\0\x26\0\0\0\0\0\0\x80\x0a\0\0\0\x16\0\x22\0", 22) == 0 &&
           memcmp(mTable + 44 + 22, "\0\x20\0\x03", 4) == 0 && Address == ExpectedUart && AddressSize == 0x1000 &&
           mTable[82] == '.' && mTable[83] == 0 && mTable[84] == 0xa5, Line);
}

//
// The DSDT for these cores, compared with nothing here: it is written to
// <Name>.aml, and the ASL that describes the same thing to <Name>.asl. run.sh
// has the ASL compiler build the latter and compares.
//
STATIC VOID WriteDsdt(IN CONST CHAR8 *Name, IN CONST APPLE_CPU *Cpus, IN UINTN Count, IN UINT32 ChipId)
{
    STATIC CONST CHAR8 Suffix[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    CHAR8              Asl[8192];
    UINTN              Used;
    UINTN              Index;
    UINTN              Size = DsdtSize(Cpus, Count);
    CHAR8              Line[256];

    memset(mTable, 0xa5, sizeof(mTable));
    snprintf(Line, sizeof(Line), "%s: the DSDT is built, %u bytes, revision 2, nothing written beyond it", Name, (unsigned)Size);
    if(!Expect(Size < sizeof(mTable) && DsdtBuild(ChipId, CREATOR_REVISION, Cpus, Count, mTable) == RETURN_SUCCESS &&
               HeaderIs((VOID *)mTable, "DSDT", Size, 2, "Apple ", ChipId) && mTable[Size - 1] != 0xa5 && mTable[Size] == 0xa5, Line)) {
        return;
    }
    WriteOutput(Name, "aml", mTable, Size);

    Used = snprintf(Asl, sizeof(Asl), "DefinitionBlock (\"\", \"DSDT\", 2, \"Apple \", \"APPLEEFI\", 0x%X) {\n  Scope (\\_SB) {\n", (unsigned)ChipId);
    for(Index = 0; Index < Count; Index++) {
        Used += snprintf(Asl + Used, sizeof(Asl) - Used, "    Device (CPU%c) { Name (_HID, \"ACPI0007\") Name (_UID, %u) }\n", Suffix[Cpus[Index].Number], (unsigned)Cpus[Index].Number);
    }
    Used += snprintf(Asl + Used, sizeof(Asl) - Used, "  }\n}\n");
    WriteOutput(Name, "asl", Asl, Used);
}

STATIC VOID WriteDsdtOfNumbers(IN CONST CHAR8 *Name, IN CONST UINT32 *Numbers, IN UINTN Count)
{
    APPLE_CPU Cpus[APPLE_MAX_CPUS + 4];
    UINTN     Index;

    memset(Cpus, 0, sizeof(Cpus));
    for(Index = 0; Index < Count; Index++) {
        Cpus[Index].Number = Numbers[Index];
    }
    WriteDsdt(Name, Cpus, Count, 0x8132);
}

#define DSDT_OF(Name, ...) \
    do { \
        STATIC CONST UINT32 Numbers[] = { __VA_ARGS__ }; \
        WriteDsdtOfNumbers(Name, Numbers, ARRAY_SIZE(Numbers)); \
    } while(0)

STATIC VOID CheckAgainstStatic(VOID)
{
    APPLE_CPU  Cpus[APPLE_MAX_CPUS];
    UINTN      Count;
    UINT64     UartBase;
    UINT32     ChipId;
    UINT8      Static[4096];
    UINTN      Size;

    if(!Expect(ReadMachine(&mM4MacBookPro, Cpus, &Count, &UartBase, &ChipId), "M4 MacBook Pro: the tree is read")) {
        return;
    }

    Size = ReadStatic("Madt.acpi", Static, sizeof(Static));
    Expect(Size == MadtSize(Count) && MadtBuild(ChipId, CREATOR_REVISION, Cpus, Count, DISTRIBUTOR_BASE, REDISTRIBUTORS_BASE, mTable) == RETURN_SUCCESS &&
           memcmp(mTable, Static, Size) == 0, "M4 MacBook Pro: the MADT is the static one of 816ea14, all 884 bytes");

    Size = ReadStatic("Dbg2.acpi", Static, sizeof(Static));
    Dbg2Build(ChipId, CREATOR_REVISION, UartBase, mTable);
    Expect(Size == Dbg2Size() && memcmp(mTable, Static, Size) == 0, "M4 MacBook Pro: the DBG2 is the static one of 816ea14, all 84 bytes");

    //
    // The static DSDT came out of the ASL compiler, which signs a table with its
    // own creator ID and revision (bytes 28-35) and fills in the checksum (byte 9).
    //
    Size = ReadStatic("Dsdt.aml", Static, sizeof(Static));
    Expect(Size == DsdtSize(Cpus, Count) && DsdtBuild(ChipId, CREATOR_REVISION, Cpus, Count, mTable) == RETURN_SUCCESS &&
           memcmp(mTable, Static, 9) == 0 && memcmp(mTable + 10, Static + 10, 18) == 0 && memcmp(mTable + 36, Static + 36, Size - 36) == 0,
           "M4 MacBook Pro: the DSDT is the static one of 816ea14, all 331 bytes but checksum and creator");
}

STATIC VOID CheckRefusals(VOID)
{
    APPLE_CPU Cpus[APPLE_MAX_CPUS + 1];
    UINTN     Index;

    memset(Cpus, 0, sizeof(Cpus));
    for(Index = 0; Index < ARRAY_SIZE(Cpus); Index++) {
        Cpus[Index].Number = (UINT32)Index;
    }

    Expect(MadtBuild(0x8132, 1, Cpus, 4, DISTRIBUTOR_BASE, REDISTRIBUTORS_BASE, mTable) == RETURN_INVALID_PARAMETER, "MADT: no boot core is refused");
    Cpus[1].Boot = TRUE;
    Expect(MadtBuild(0x8132, 1, Cpus, 4, DISTRIBUTOR_BASE, REDISTRIBUTORS_BASE, mTable) == RETURN_SUCCESS, "MADT: one boot core is taken");
    Expect(MadtBuild(0x8132, 1, Cpus, APPLE_MAX_CPUS, DISTRIBUTOR_BASE, REDISTRIBUTORS_BASE, mTable) == RETURN_SUCCESS, "MADT: 32 cores are taken");
    Expect(MadtBuild(0x8132, 1, Cpus, APPLE_MAX_CPUS + 1, DISTRIBUTOR_BASE, REDISTRIBUTORS_BASE, mTable) == RETURN_INVALID_PARAMETER, "MADT: 33 cores are refused");
    Expect(MadtBuild(0x8132, 1, Cpus, 0, DISTRIBUTOR_BASE, REDISTRIBUTORS_BASE, mTable) == RETURN_INVALID_PARAMETER, "MADT: no cores are refused");
    Cpus[3].Boot = TRUE;
    Expect(MadtBuild(0x8132, 1, Cpus, 4, DISTRIBUTOR_BASE, REDISTRIBUTORS_BASE, mTable) == RETURN_INVALID_PARAMETER, "MADT: two boot cores are refused");

    Cpus[2].Number = 36;
    memset(mTable, 0xa5, sizeof(mTable));
    Expect(DsdtBuild(0x8132, 1, Cpus, 4, mTable) == RETURN_INVALID_PARAMETER && mTable[0] == 0xa5, "DSDT: core number 36 is refused, nothing is written");

    Expect(EmulatedGicRedistributorCount(Cpus, 0) == 0, "no cores: no Redistributors");
}

int main(int argc, char **argv)
{
    STATIC CONST EXPECTED_GICC M4[] = {
        { 6, 0x10100, 1 }, { 7, 0x10101, 1 }, { 8, 0x10102, 1 }, { 9, 0x10103, 1 },
        { 0, 0x00000, 0 }, { 1, 0x00001, 0 }, { 2, 0x00002, 0 }, { 3, 0x00003, 0 }, { 4, 0x00004, 0 }, { 5, 0x00005, 0 },
    };
    STATIC CONST EXPECTED_GICC Other[] = {
        { 2, 0x10100, 1 }, { 3, 0x10101, 1 }, { 4, 0x10102, 1 }, { 5, 0x10200, 1 }, { 6, 0x10201, 1 }, { 7, 0x10202, 1 },
        { 0, 0x00000, 0 }, { 1, 0x00001, 0 },
    };
    STATIC CONST EXPECTED_GICC Sparse[] = {
        { 3, 0x00003, 0 }, { 9, 0x10101, 1 }, { 1, 0x00001, 0 },
    };
    APPLE_CPU Cpus[APPLE_MAX_CPUS];
    UINTN     Count;
    UINT64    UartBase;
    UINT32    ChipId;

    if(argc != 3) {
        printf("usage: %s <directory of the static tables> <directory to write to>\n", argv[0]);
        return 2;
    }
    mStaticDirectory = argv[1];
    mOutputDirectory = argv[2];

    CheckAgainstStatic();

    CheckMadt("M4 MacBook Pro", &mM4MacBookPro, M4, ARRAY_SIZE(M4), 10);
    CheckMadt("M4 MacBook Pro on one core", &mM4MacBookProOneCore, M4, 1, 7);
    CheckMadt("eight cores started on core 2", &mOther, Other, ARRAY_SIZE(Other), 8);
    CheckMadt("cores 9, 3, 1 started on core 3", &mSparse, Sparse, ARRAY_SIZE(Sparse), 10);

    CheckDbg2("M4 MacBook Pro", &mM4MacBookPro, 0x3ad200000ULL);
    CheckDbg2("eight cores started on core 2", &mOther, 0x2b9200000ULL);

    if(Expect(ReadMachine(&mM4MacBookPro, Cpus, &Count, &UartBase, &ChipId), "M4 MacBook Pro: the tree is read")) {
        WriteDsdt("m4-macbook-pro", Cpus, Count, ChipId);
    }
    if(Expect(ReadMachine(&mM4MacBookProOneCore, Cpus, &Count, &UartBase, &ChipId), "M4 MacBook Pro on one core: the tree is read")) {
        WriteDsdt("m4-macbook-pro-one-core", Cpus, Count, ChipId);
    }
    if(Expect(ReadMachine(&mOther, Cpus, &Count, &UartBase, &ChipId), "eight cores: the tree is read")) {
        WriteDsdt("eight-cores", Cpus, Count, ChipId);
    }
    if(Expect(ReadMachine(&mSparse, Cpus, &Count, &UartBase, &ChipId), "cores 9, 3, 1: the tree is read")) {
        WriteDsdt("cores-9-3-1", Cpus, Count, ChipId);
    }
    // The scope with cores 2 and 3 is 63 bytes long, the most one length byte holds; one core more needs two
    DSDT_OF("core-0", 0);
    DSDT_OF("cores-0-1", 0, 1);
    DSDT_OF("cores-2-3", 2, 3);
    DSDT_OF("cores-0-1-2", 0, 1, 2);
    DSDT_OF("cores-10-35", 10, 35);
    DSDT_OF("cores-0-to-31", 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31);

    CheckRefusals();

    return Verdict();
}
