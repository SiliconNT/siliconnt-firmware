/** @file
  Host test of the ACPI tables the firmware builds at boot (MADT, DBG2, DSDT):
  from a device tree the test builds, through Library/AppleDTLib, to the bytes
  Drivers/AcpiPlatformDxe/AcpiTableBuilder.c produces.

  For the tree of the M4 MacBook Pro the tables must be the ones the firmware had
  as static tables before (commit 816ea14, kept in Static816ea14/), byte for
  byte. Other trees are checked field by field. Every DSDT is also written out
  next to the ASL that describes it, for run.sh to compare with what the ASL
  compiler makes of that ASL.

  The node of the internal disk controller (Drivers/AcpiPlatformDxe/AnsNode.c):
  from the M4 MacBook Pro's tree with its power manager and its table of
  interrupt numbers, the values it must have; from trees that lack something, no
  node and the DSDT of the processors alone, the static one. With the node, the
  DSDT is the one of commit 979ab44 (Static979ab44/), byte for byte.

  The nodes of MTP and SMC (Drivers/AcpiPlatformDxe/CoprocessorNode.c):
  CoprocessorNodeTest.h. How AML is written (AmlWriter.c): AmlTest.h.

  Without a serial port (the tree has no /arm-io/uart0, as m1n1 hands it over
  when no host is attached): no DBG2, and the MADT and the DSDT are the same as
  with one.

  Usage: AcpiTablesTest <directory of the static tables> <directory to write to>
         [<device tree> <ASL of its MTP and SMC nodes>].
  Run it with ./run.sh.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "../../../../Silicon/Apple/AppleSiliconPkg/Test/AppleDTLibHostTest/HostTest.h"

#include "../../Drivers/AcpiPlatformDxe/AmlWriter.c"
#include "../../Drivers/AcpiPlatformDxe/AcpiTableBuilder.c"
#include "../../Drivers/AcpiPlatformDxe/AnsNode.c"
#include "../../Drivers/AcpiPlatformDxe/DeviceProperties.c"
#include "../../Drivers/AcpiPlatformDxe/CoprocessorNode.c"

#include <stdlib.h>

//
// What the EL2 layer emulates and what the platform description file sets
//
#define DISTRIBUTOR_BASE     0x5000000000ULL
#define REDISTRIBUTORS_BASE  0x5100000000ULL
#define CREATOR_REVISION     1

STATIC CONST CHAR8 *mStaticDirectory;
STATIC CONST CHAR8 *mOutputDirectory;

STATIC UINT8 mTable[131072];

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

// Where the tables of commit 979ab44 are, next to those of 816ea14
#define STATIC_979AB44  "../Static979ab44/"

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

// The serial port as the entry code reads it (PrePi/AdtParser.c): no /arm-io/uart0 is no port, address 0
STATIC BOOLEAN ReadSerialPort(IN CONST APPLE_BOOT_INFO *Info, OUT UINT64 *UartBase)
{
    RETURN_STATUS Status = AppleGetUartBase(Info, UartBase);

    return Status == RETURN_SUCCESS || Status == RETURN_NOT_FOUND;
}

// What the firmware reads from the tree of Machine: the serial port as the entry code does, the rest as AcpiPlatformDxe does
STATIC BOOLEAN ReadMachine(IN CONST TEST_MACHINE *Machine, OUT APPLE_CPU *Cpus, OUT UINTN *Count, OUT UINT64 *UartBase, OUT UINT32 *ChipId)
{
    BuildMachine(Machine);
    return dt_check(mTree, mUsed, NULL) == 0 &&
           AppleGetCpus(BuiltTree(), Cpus, Count) == RETURN_SUCCESS &&
           ReadSerialPort(BuiltTree(), UartBase) &&
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
// The DSDT with the disk controller and no other device; Ans NULL: without any
//
STATIC UINTN DsdtSizeWith(IN CONST APPLE_CPU *Cpus, IN UINTN CpuCount, IN CONST DSDT_ANS *Ans)
{
    ANS_DEVICE         Device;
    CONST DSDT_DEVICE  *Devices[1] = { &Device.Dsdt };

    if(Ans != NULL) {
        DescribeAns(Ans, &Device);
    }
    return DsdtSize(Cpus, CpuCount, Devices, Ans != NULL ? 1 : 0);
}

STATIC RETURN_STATUS DsdtBuildWith(IN UINT32 OemRevision, IN UINT32 CreatorRevision, IN CONST APPLE_CPU *Cpus, IN UINTN CpuCount, IN CONST DSDT_ANS *Ans, OUT VOID *Table)
{
    ANS_DEVICE         Device;
    CONST DSDT_DEVICE  *Devices[1] = { &Device.Dsdt };

    if(Ans != NULL) {
        DescribeAns(Ans, &Device);
    }
    return DsdtBuild(OemRevision, CreatorRevision, Cpus, CpuCount, Devices, Ans != NULL ? 1 : 0, Table);
}

//
// The ASL of the disk controller's node, written here from the ACPI and _DSD
// specifications and not from the builder: the hardware ID, the UUID and the key
// are spelled out again.
//
STATIC UINTN WriteAnsAsl(OUT CHAR8 *Asl, IN UINTN Room, IN CONST DSDT_ANS *Ans)
{
    CONST APPLE_MMIO_RANGE *Ranges[4] = { &Ans->Nvme, &Ans->Coprocessor, &Ans->Sart, &Ans->Pmgr };
    UINTN                  Used;
    UINTN                  Index;

    Used = snprintf(Asl, Room,
                    "    Device (ANS0) {\n"
                    "      Name (_HID, \"SLNT0001\")\n"
                    "      Name (_UID, 0)\n"
                    "      Name (_CCA, 0)\n"
                    "      Name (_CRS, ResourceTemplate () {\n");
    for(Index = 0; Index < ARRAY_SIZE(Ranges); Index++) {
        Used += snprintf(Asl + Used, Room - Used,
                         "        QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0x0, 0x%llX, 0x%llX, 0x0, 0x%llX)\n",
                         (unsigned long long)Ranges[Index]->Base, (unsigned long long)(Ranges[Index]->Base + (Ranges[Index]->Size - 1)),
                         (unsigned long long)Ranges[Index]->Size);
    }
    Used += snprintf(Asl + Used, Room - Used,
                     "        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { %u }\n"
                     "        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { %u }\n"
                     "      })\n",
                     (unsigned)Ans->NvmeGsiv, (unsigned)Ans->MailboxGsiv);
    Used += snprintf(Asl + Used, Room - Used,
                     "      Name (_DSD, Package () {\n"
                     "        ToUUID (\"daffd814-6eba-4d8c-8a91-bc9bbf4aa301\"),\n"
                     "        Package () {\n");
    if(Ans->SecureBar) {
        Used += snprintf(Asl + Used, Room - Used, "          Package (2) { \"nvme-secure-bar\", 1 },\n");
    }
    Used += snprintf(Asl + Used, Room - Used,
                     "          Package (2) { \"sart-version\", %u }\n"
                     "        }\n"
                     "      })\n"
                     "    }\n",
                     (unsigned)Ans->SartVersion);
    return Used;
}

//
// The DSDT for these cores and this disk controller (NULL: none), compared with
// nothing here: it is written to <Name>.aml, and the ASL that describes the same
// thing to <Name>.asl. run.sh has the ASL compiler build the latter and compares.
//
STATIC VOID WriteDsdt(IN CONST CHAR8 *Name, IN CONST APPLE_CPU *Cpus, IN UINTN Count, IN CONST DSDT_ANS *Ans, IN UINT32 ChipId)
{
    STATIC CONST CHAR8 Suffix[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    CHAR8              Asl[8192];
    UINTN              Used;
    UINTN              Index;
    UINTN              Size = DsdtSizeWith(Cpus, Count, Ans);
    CHAR8              Line[256];

    memset(mTable, 0xa5, sizeof(mTable));
    snprintf(Line, sizeof(Line), "%s: the DSDT is built, %u bytes, revision 2, nothing written beyond it", Name, (unsigned)Size);
    if(!Expect(Size < sizeof(mTable) && DsdtBuildWith(ChipId, CREATOR_REVISION, Cpus, Count, Ans, mTable) == RETURN_SUCCESS &&
               HeaderIs((VOID *)mTable, "DSDT", Size, 2, "Apple ", ChipId) && mTable[Size - 1] != 0xa5 && mTable[Size] == 0xa5, Line)) {
        return;
    }
    WriteOutput(Name, "aml", mTable, Size);

    Used = snprintf(Asl, sizeof(Asl), "DefinitionBlock (\"\", \"DSDT\", 2, \"Apple \", \"APPLEEFI\", 0x%X) {\n  Scope (\\_SB) {\n", (unsigned)ChipId);
    for(Index = 0; Index < Count; Index++) {
        Used += snprintf(Asl + Used, sizeof(Asl) - Used, "    Device (CPU%c) { Name (_HID, \"ACPI0007\") Name (_UID, %u) }\n", Suffix[Cpus[Index].Number], (unsigned)Cpus[Index].Number);
    }
    if(Ans != NULL) {
        Used += WriteAnsAsl(Asl + Used, sizeof(Asl) - Used, Ans);
    }
    Used += snprintf(Asl + Used, sizeof(Asl) - Used, "  }\n}\n");
    WriteOutput(Name, "asl", Asl, Used);
}

STATIC VOID WriteDsdtOfNumbers(IN CONST CHAR8 *Name, IN CONST UINT32 *Numbers, IN UINTN Count, IN CONST DSDT_ANS *Ans)
{
    APPLE_CPU Cpus[APPLE_MAX_CPUS + 4];
    UINTN     Index;

    memset(Cpus, 0, sizeof(Cpus));
    for(Index = 0; Index < Count; Index++) {
        Cpus[Index].Number = Numbers[Index];
    }
    WriteDsdt(Name, Cpus, Count, Ans, 0x8132);
}

#define DSDT_OF(Name, ...) \
    do { \
        STATIC CONST UINT32 Numbers[] = { __VA_ARGS__ }; \
        WriteDsdtOfNumbers(Name, Numbers, ARRAY_SIZE(Numbers), NULL); \
    } while(0)

//
// The disk controller's node the M4 MacBook Pro must get: the registers of
// /arm-io/ans and /arm-io/sart-ans (SiliconNT findings/0057, conclusion 20, and
// findings/0062), the controller's register in the power manager at
// 0x3_8070_0538, four bytes (findings/0071, conclusion 31), AIC 1018 and 1006 as
// GIC 258 and 257, "nvme-secure-bar", SART version 3 (conclusion 32)
//
STATIC CONST DSDT_ANS mM4Node = {
    { 0x4c5cc0000ULL, 0x60000 },
    { 0x481600000ULL, 0x88000 },
    { 0x485c50000ULL, 0xc000 },
    { 0x380700538ULL, 4 },
    258, 257, TRUE, 3
};

STATIC BOOLEAN SameNode(IN CONST DSDT_ANS *A, IN CONST DSDT_ANS *B)
{
    return A->Nvme.Base == B->Nvme.Base && A->Nvme.Size == B->Nvme.Size &&
           A->Coprocessor.Base == B->Coprocessor.Base && A->Coprocessor.Size == B->Coprocessor.Size &&
           A->Sart.Base == B->Sart.Base && A->Sart.Size == B->Sart.Size &&
           A->Pmgr.Base == B->Pmgr.Base && A->Pmgr.Size == B->Pmgr.Size &&
           A->NvmeGsiv == B->NvmeGsiv && A->MailboxGsiv == B->MailboxGsiv && A->SecureBar == B->SecureBar &&
           A->SartVersion == B->SartVersion;
}

//
// What AcpiPlatformDxe does with the tree of Machine: read the cores and the disk
// controller, build the DSDT with the node if there is one. Checks the outcome
// and, with a node, its values. Writes the DSDT and its ASL as <Name>.
//
STATIC VOID CheckAnsNode(IN CONST CHAR8 *Name, IN CONST TEST_MACHINE *Machine, IN ANS_NODE_OUTCOME ExpectedOutcome, IN CONST DSDT_ANS *ExpectedNode)
{
    STATIC CONST DSDT_ANS Zero;
    APPLE_CPU             Cpus[APPLE_MAX_CPUS];
    UINTN                 Count;
    UINT64                UartBase;
    UINT32                ChipId;
    ANS_NODE              Ans;
    CHAR8                 Line[256];

    snprintf(Line, sizeof(Line), "%s: outcome %d, %s", Name, (int)ExpectedOutcome, ExpectedNode != NULL ? "the node has the values it must have" : "no node");
    if(!Expect(ReadMachine(Machine, Cpus, &Count, &UartBase, &ChipId), Line)) {
        return;
    }
    memset(&Ans, 0x5a, sizeof(Ans));
    FindAnsNode(BuiltTree(), &Ans);
    if(!Expect(Ans.Outcome == ExpectedOutcome && SameNode(&Ans.Node, ExpectedNode != NULL ? ExpectedNode : &Zero), Line)) {
        printf("      outcome %d status 0x%llx nvme 0x%llx+0x%llx coprocessor 0x%llx+0x%llx sart 0x%llx+0x%llx pmgr 0x%llx+0x%llx gsiv %u %u secure-bar %d sart-version %u\n",
               (int)Ans.Outcome, (unsigned long long)Ans.Status, (unsigned long long)Ans.Node.Nvme.Base, (unsigned long long)Ans.Node.Nvme.Size,
               (unsigned long long)Ans.Node.Coprocessor.Base, (unsigned long long)Ans.Node.Coprocessor.Size,
               (unsigned long long)Ans.Node.Sart.Base, (unsigned long long)Ans.Node.Sart.Size,
               (unsigned long long)Ans.Node.Pmgr.Base, (unsigned long long)Ans.Node.Pmgr.Size,
               (unsigned)Ans.Node.NvmeGsiv, (unsigned)Ans.Node.MailboxGsiv, (int)Ans.Node.SecureBar, (unsigned)Ans.Node.SartVersion);
        return;
    }
    WriteDsdt(Name, Cpus, Count, Ans.Outcome == AnsNodeMade ? &Ans.Node : NULL, ChipId);
}

//
// A tree from which no node is made gives the DSDT the firmware built before
// there was one: for the cores of the M4 MacBook Pro, the static DSDT of 816ea14
// but for checksum and creator (CheckAgainstStatic())
//
STATIC VOID CheckNoNodeIsStatic(IN CONST CHAR8 *What, IN CONST TEST_MACHINE *Machine)
{
    APPLE_CPU  Cpus[APPLE_MAX_CPUS];
    UINTN      Count;
    UINT64     UartBase;
    UINT32     ChipId;
    ANS_NODE   Ans;
    UINT8      Static[4096];
    UINTN      Size;

    Size = ReadStatic("Dsdt.aml", Static, sizeof(Static));
    if(!ReadMachine(Machine, Cpus, &Count, &UartBase, &ChipId)) {
        Expect(FALSE, What);
        return;
    }
    FindAnsNode(BuiltTree(), &Ans);
    Expect(Ans.Outcome != AnsNodeMade && Size == DsdtSizeWith(Cpus, Count, NULL) &&
           DsdtBuildWith(ChipId, CREATOR_REVISION, Cpus, Count, NULL, mTable) == RETURN_SUCCESS &&
           memcmp(mTable, Static, 9) == 0 && memcmp(mTable + 10, Static + 10, 18) == 0 && memcmp(mTable + 36, Static + 36, Size - 36) == 0, What);
}

// The M4 MacBook Pro as m1n1 hands it over, with another disk controller or another table
STATIC TEST_MACHINE M4With(IN CONST TEST_ANS *Ans, IN CONST UINT32 *AicToGic, IN UINT32 AicToGicCount)
{
    TEST_MACHINE Machine = mM4MacBookProWithAns;

    Machine.Ans           = Ans;
    Machine.AicToGic      = AicToGic;
    Machine.AicToGicCount = AicToGicCount;
    return Machine;
}

STATIC VOID CheckAnsNodes(VOID)
{
    STATIC UINT32 WithoutNvmeLine[ARRAY_SIZE(mAicToGicOfM4MacBookPro)];
    STATIC UINT32 WithoutMailboxLine[ARRAY_SIZE(mAicToGicOfM4MacBookPro)];
    STATIC CONST UINT32 Descending[] = { 1018, 1006 };
    TEST_MACHINE  Machine;
    TEST_ANS      Ans;
    DSDT_ANS      Node;
    UINT32        Count = 0;
    UINT32        CountMailbox = 0;
    UINTN         Index;

    for(Index = 0; Index < ARRAY_SIZE(mAicToGicOfM4MacBookPro); Index++) {
        if(mAicToGicOfM4MacBookPro[Index] != 1018) {
            WithoutNvmeLine[Count++] = mAicToGicOfM4MacBookPro[Index];
        }
        if(mAicToGicOfM4MacBookPro[Index] != 1006) {
            WithoutMailboxLine[CountMailbox++] = mAicToGicOfM4MacBookPro[Index];
        }
    }

    //
    // A node
    //
    CheckAnsNode("m4-macbook-pro-ans", &mM4MacBookProWithAns, AnsNodeMade, &mM4Node);

    Ans           = mAnsOfM4MacBookPro;
    Ans.SecureBar = FALSE;
    Machine       = M4With(&Ans, mAicToGicOfM4MacBookPro, ARRAY_SIZE(mAicToGicOfM4MacBookPro));
    Node          = mM4Node;
    Node.Nvme.Base = 0x485cc0000ULL;
    Node.SecureBar = FALSE;
    CheckAnsNode("m4-macbook-pro-ans-no-secure-bar", &Machine, AnsNodeMade, &Node);

    // Only the lines of the disk controller in the table: they are GIC 32 to 36
    {
        STATIC CONST UINT32 Lines[] = { 1003, 1004, 1005, 1006, 1018 };

        Machine          = M4With(&mAnsOfM4MacBookPro, Lines, ARRAY_SIZE(Lines));
        Node             = mM4Node;
        Node.MailboxGsiv = 35;
        Node.NvmeGsiv    = 36;
        CheckAnsNode("m4-macbook-pro-ans-short-table", &Machine, AnsNodeMade, &Node);
    }

    //
    // No node; the DSDT is the one without it
    //
    CheckAnsNode("no-node-no-disk-controller", &mM4MacBookPro, AnsNodeNoDevice, NULL);
    Machine = M4With(NULL, mAicToGicOfM4MacBookPro, ARRAY_SIZE(mAicToGicOfM4MacBookPro));
    CheckAnsNode("no-node-no-disk-controller-but-a-table", &Machine, AnsNodeNoDevice, NULL);
    CheckNoNodeIsStatic("a table and no /arm-io/ans: the static DSDT of 816ea14", &Machine);

    Machine = M4With(&mAnsOfM4MacBookPro, NULL, 0);
    CheckAnsNode("no-node-no-table", &Machine, AnsNodeNoTable, NULL);
    CheckNoNodeIsStatic("/arm-io/ans and no AICToGIC: the static DSDT of 816ea14", &Machine);

    Machine = M4With(&mAnsOfM4MacBookPro, Descending, ARRAY_SIZE(Descending));
    CheckAnsNode("no-node-unusable-table", &Machine, AnsNodeNoTable, NULL);

    Machine = M4With(&mAnsOfM4MacBookPro, WithoutNvmeLine, Count);
    CheckAnsNode("no-node-no-controller-line", &Machine, AnsNodeNvmeLineMissing, NULL);
    CheckNoNodeIsStatic("a table without AIC 1018: the static DSDT of 816ea14", &Machine);

    Machine = M4With(&mAnsOfM4MacBookPro, WithoutMailboxLine, CountMailbox);
    CheckAnsNode("no-node-no-mailbox-line", &Machine, AnsNodeMailboxLineMissing, NULL);

    Ans      = mAnsOfM4MacBookPro;
    Ans.Omit = OMIT_ANS_INTERRUPTS;
    Machine  = M4With(&Ans, mAicToGicOfM4MacBookPro, ARRAY_SIZE(mAicToGicOfM4MacBookPro));
    CheckAnsNode("no-node-no-interrupts", &Machine, AnsNodeNoDevice, NULL);
    CheckNoNodeIsStatic("/arm-io/ans without \"interrupts\": the static DSDT of 816ea14", &Machine);

    Ans.Omit = OMIT_SART;
    Machine  = M4With(&Ans, mAicToGicOfM4MacBookPro, ARRAY_SIZE(mAicToGicOfM4MacBookPro));
    CheckAnsNode("no-node-no-sart", &Machine, AnsNodeNoDevice, NULL);

    Ans.Omit = OMIT_SART_VERSION;
    Machine  = M4With(&Ans, mAicToGicOfM4MacBookPro, ARRAY_SIZE(mAicToGicOfM4MacBookPro));
    CheckAnsNode("no-node-no-sart-version", &Machine, AnsNodeNoSartVersion, NULL);
    CheckNoNodeIsStatic("sart-ans without \"sart-version\": the static DSDT of 816ea14", &Machine);

    //
    // The power manager: none, no entry for the controller, two
    //
    {
        UINT8     Devices[sizeof(mPmgrDevicesOfM4MacBookPro)];
        TEST_PMGR Pmgr = mPmgrOfM4MacBookPro;

        Machine      = mM4MacBookProWithAns;
        Machine.Pmgr = NULL;
        CheckAnsNode("no-node-no-power-manager", &Machine, AnsNodeNoPmgr, NULL);
        CheckNoNodeIsStatic("no /arm-io/pmgr: the static DSDT of 816ea14", &Machine);

        memcpy(Devices, mPmgrDevicesOfM4MacBookPro, sizeof(Devices));
        memcpy(Devices + PMGR_DEVICE_INDEX_ANS * 48 + 32, "ANX", 3);
        Pmgr.Devices = Devices;
        Machine.Pmgr = &Pmgr;
        CheckAnsNode("no-node-no-ans-in-the-power-manager", &Machine, AnsNodeNoPmgr, NULL);
        CheckNoNodeIsStatic("no entry ANS or ANS2 in \"devices\": the static DSDT of 816ea14", &Machine);

        memcpy(Devices + PMGR_DEVICE_INDEX_ANS * 48 + 32, "ANS", 3);
        memcpy(Devices + 3 * 48 + 32, "ANS2\0\0\0\0", 8);
        CheckAnsNode("no-node-ans-and-ans2", &Machine, AnsNodeNoPmgr, NULL);
        CheckNoNodeIsStatic("entries ANS and ANS2 in \"devices\": the static DSDT of 816ea14", &Machine);
    }

    //
    // The node in other DSDTs: alone, after 32 cores, at the top of the address space
    //
    WriteDsdtOfNumbers("ans-alone", NULL, 0, &mM4Node);
    {
        STATIC CONST UINT32 Numbers[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 };

        WriteDsdtOfNumbers("cores-0-to-31-ans", Numbers, ARRAY_SIZE(Numbers), &mM4Node);
    }
    Node                  = mM4Node;
    Node.Nvme.Base        = 0xfffffffffffa0000ULL;
    Node.Coprocessor.Base = 0;
    Node.Coprocessor.Size = 1;
    Node.Pmgr.Base        = 0xfffffffffffffffcULL;
    Node.NvmeGsiv         = 1019;
    Node.MailboxGsiv      = 32;
    Node.SecureBar        = FALSE;
    Node.SartVersion      = 0;
    WriteDsdtOfNumbers("ans-edges", (CONST UINT32[]){ 6 }, 1, &Node);

    // SART versions the ASL compiler writes as One, a word, a double word
    Node             = mM4Node;
    Node.SartVersion = 1;
    WriteDsdtOfNumbers("ans-sart-version-1", NULL, 0, &Node);
    Node.SartVersion = 0x100;
    WriteDsdtOfNumbers("ans-sart-version-word", NULL, 0, &Node);
    Node.SecureBar   = FALSE;
    Node.SartVersion = 0xffffffff;
    WriteDsdtOfNumbers("ans-sart-version-dword", NULL, 0, &Node);
}

//
// The DSDT of the M4 MacBook Pro with its disk controller is the one the firmware
// built before other devices came, commit 979ab44: all of it, the checksum is 0 in
// both
//
STATIC VOID CheckAnsAgainst979ab44(VOID)
{
    APPLE_CPU  Cpus[APPLE_MAX_CPUS];
    UINTN      Count;
    UINT64     UartBase;
    UINT32     ChipId;
    ANS_NODE   Ans;
    UINT8      Static[4096];
    UINTN      Size;

    Size = ReadStatic(STATIC_979AB44 "Dsdt.aml", Static, sizeof(Static));
    if(!Expect(ReadMachine(&mM4MacBookProWithAns, Cpus, &Count, &UartBase, &ChipId), "M4 MacBook Pro with its disk controller: the tree is read")) {
        return;
    }
    FindAnsNode(BuiltTree(), &Ans);
    Expect(Ans.Outcome == AnsNodeMade && Size == 652 && Size == DsdtSizeWith(Cpus, Count, &Ans.Node) &&
           DsdtBuildWith(ChipId, CREATOR_REVISION, Cpus, Count, &Ans.Node, mTable) == RETURN_SUCCESS &&
           memcmp(mTable, Static, Size) == 0, "M4 MacBook Pro with its disk controller: the DSDT is the one of 979ab44, all 652 bytes");
}

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
    Expect(Size == DsdtSizeWith(Cpus, Count, NULL) && DsdtBuildWith(ChipId, CREATOR_REVISION, Cpus, Count, NULL, mTable) == RETURN_SUCCESS &&
           memcmp(mTable, Static, 9) == 0 && memcmp(mTable + 10, Static + 10, 18) == 0 && memcmp(mTable + 36, Static + 36, Size - 36) == 0,
           "M4 MacBook Pro: the DSDT is the static one of 816ea14, all 331 bytes but checksum and creator");
}

//
// A DBG2 only with a serial port. Without one, the tables that are installed are
// the same as with one: for the M4 MacBook Pro with its disk controller, the MADT
// and the DSDT with the controller's node, byte for byte.
//
STATIC VOID CheckWithoutSerialPort(VOID)
{
    STATIC CONST struct {
        CONST CHAR8 *What;
        TEST_UART   Uart;
    } Cases[] = {
        { "the name of uart0 cleared, as m1n1 hands the tree over without a host", TestUartNameCleared },
        { "no uart0", TestUartLeftOut },
    };
    STATIC UINT8  Madt[4096];
    STATIC UINT8  Dsdt[4096];
    TEST_MACHINE  Machine = mM4MacBookProWithAns;
    APPLE_CPU     Cpus[APPLE_MAX_CPUS];
    UINTN         Count;
    UINT64        UartBase;
    UINT32        ChipId;
    ANS_NODE      Ans;
    UINTN         MadtLength;
    UINTN         DsdtLength;
    UINTN         Index;
    CHAR8         Line[256];

    if(!Expect(ReadMachine(&Machine, Cpus, &Count, &UartBase, &ChipId), "M4 MacBook Pro with its disk controller: the tree is read")) {
        return;
    }
    FindAnsNode(BuiltTree(), &Ans);
    if(!Expect(Dbg2Wanted(UartBase) && UartBase == 0x3ad200000ULL && Ans.Outcome == AnsNodeMade &&
               MadtBuild(ChipId, CREATOR_REVISION, Cpus, Count, DISTRIBUTOR_BASE, REDISTRIBUTORS_BASE, Madt) == RETURN_SUCCESS &&
               DsdtBuildWith(ChipId, CREATOR_REVISION, Cpus, Count, &Ans.Node, Dsdt) == RETURN_SUCCESS,
               "M4 MacBook Pro with uart0: a DBG2 for the port at 0x3_ad20_0000; MADT, DSDT with the disk controller")) {
        return;
    }
    MadtLength = MadtSize(Count);
    DsdtLength = DsdtSizeWith(Cpus, Count, &Ans.Node);

    for(Index = 0; Index < ARRAY_SIZE(Cases); Index++) {
        Machine.Uart = Cases[Index].Uart;
        snprintf(Line, sizeof(Line), "%s: the tree is read, no serial port, no DBG2", Cases[Index].What);
        if(!Expect(ReadMachine(&Machine, Cpus, &Count, &UartBase, &ChipId) && UartBase == 0 && !Dbg2Wanted(UartBase), Line)) {
            continue;
        }
        FindAnsNode(BuiltTree(), &Ans);
        snprintf(Line, sizeof(Line), "%s: MADT and DSDT with the disk controller as with uart0, byte for byte", Cases[Index].What);
        Expect(Ans.Outcome == AnsNodeMade && MadtSize(Count) == MadtLength &&
               MadtBuild(ChipId, CREATOR_REVISION, Cpus, Count, DISTRIBUTOR_BASE, REDISTRIBUTORS_BASE, mTable) == RETURN_SUCCESS &&
               memcmp(mTable, Madt, MadtLength) == 0 &&
               DsdtSizeWith(Cpus, Count, &Ans.Node) == DsdtLength &&
               DsdtBuildWith(ChipId, CREATOR_REVISION, Cpus, Count, &Ans.Node, mTable) == RETURN_SUCCESS &&
               memcmp(mTable, Dsdt, DsdtLength) == 0, Line);
    }
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
    Expect(DsdtBuildWith(0x8132, 1, Cpus, 4, NULL, mTable) == RETURN_INVALID_PARAMETER && mTable[0] == 0xa5, "DSDT: core number 36 is refused, nothing is written");
    Cpus[2].Number = 2;

    Expect(EmulatedGicRedistributorCount(Cpus, 0) == 0, "no cores: no Redistributors");

    {
        DSDT_ANS Node;

        Node           = mM4Node;
        Node.Nvme.Size = 0;
        memset(mTable, 0xa5, sizeof(mTable));
        Expect(DsdtBuildWith(0x8132, 1, Cpus, 4, &Node, mTable) == RETURN_INVALID_PARAMETER && mTable[0] == 0xa5, "DSDT: an empty NVMe range is refused, nothing is written");
        Node           = mM4Node;
        Node.Sart.Size = 0;
        Expect(DsdtBuildWith(0x8132, 1, Cpus, 4, &Node, mTable) == RETURN_INVALID_PARAMETER && mTable[0] == 0xa5, "DSDT: an empty SART range is refused");
        Node           = mM4Node;
        Node.Pmgr.Size = 0;
        Expect(DsdtBuildWith(0x8132, 1, Cpus, 4, &Node, mTable) == RETURN_INVALID_PARAMETER && mTable[0] == 0xa5, "DSDT: an empty power manager range is refused");
        Node.Pmgr.Base = 0xfffffffffffffffdULL;
        Node.Pmgr.Size = 4;
        Expect(DsdtBuildWith(0x8132, 1, Cpus, 4, &Node, mTable) == RETURN_INVALID_PARAMETER && mTable[0] == 0xa5, "DSDT: a power manager range past the top of the address space is refused");
        Node                  = mM4Node;
        Node.Coprocessor.Base = 0;
        Node.Coprocessor.Size = 0;
        Expect(DsdtBuildWith(0x8132, 1, Cpus, 4, &Node, mTable) == RETURN_INVALID_PARAMETER && mTable[0] == 0xa5, "DSDT: an empty range at address 0 is refused");
        Node                  = mM4Node;
        Node.Coprocessor.Base = 0xfffffffffffff000ULL;
        Expect(DsdtBuildWith(0x8132, 1, Cpus, 4, &Node, mTable) == RETURN_INVALID_PARAMETER && mTable[0] == 0xa5, "DSDT: a coprocessor range past the top of the address space is refused");
        Node.Coprocessor.Base = 0xfffffffffff78000ULL;
        Expect(DsdtBuildWith(0x8132, 1, Cpus, 4, &Node, mTable) == RETURN_SUCCESS, "DSDT: a coprocessor range that ends at the top is taken");
    }
}

#include "AmlTest.h"
#include "CoprocessorNodeTest.h"

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

    if(argc != 3 && argc != 5) {
        printf("usage: %s <directory of the static tables> <directory to write to> [<device tree> <ASL of its MTP and SMC nodes>]\n", argv[0]);
        return 2;
    }
    mStaticDirectory = argv[1];
    mOutputDirectory = argv[2];

    CheckAgainstStatic();
    CheckAnsAgainst979ab44();
    CheckAml();

    CheckMadt("M4 MacBook Pro", &mM4MacBookPro, M4, ARRAY_SIZE(M4), 10);
    CheckMadt("M4 MacBook Pro on one core", &mM4MacBookProOneCore, M4, 1, 7);
    CheckMadt("eight cores started on core 2", &mOther, Other, ARRAY_SIZE(Other), 8);
    CheckMadt("cores 9, 3, 1 started on core 3", &mSparse, Sparse, ARRAY_SIZE(Sparse), 10);

    CheckDbg2("M4 MacBook Pro", &mM4MacBookPro, 0x3ad200000ULL);
    CheckDbg2("eight cores started on core 2", &mOther, 0x2b9200000ULL);

    if(Expect(ReadMachine(&mM4MacBookPro, Cpus, &Count, &UartBase, &ChipId), "M4 MacBook Pro: the tree is read")) {
        WriteDsdt("m4-macbook-pro", Cpus, Count, NULL, ChipId);
    }
    if(Expect(ReadMachine(&mM4MacBookProOneCore, Cpus, &Count, &UartBase, &ChipId), "M4 MacBook Pro on one core: the tree is read")) {
        WriteDsdt("m4-macbook-pro-one-core", Cpus, Count, NULL, ChipId);
    }
    if(Expect(ReadMachine(&mOther, Cpus, &Count, &UartBase, &ChipId), "eight cores: the tree is read")) {
        WriteDsdt("eight-cores", Cpus, Count, NULL, ChipId);
    }
    if(Expect(ReadMachine(&mSparse, Cpus, &Count, &UartBase, &ChipId), "cores 9, 3, 1: the tree is read")) {
        WriteDsdt("cores-9-3-1", Cpus, Count, NULL, ChipId);
    }
    // The scope with cores 2 and 3 is 63 bytes long, the most one length byte holds; one core more needs two
    DSDT_OF("core-0", 0);
    DSDT_OF("cores-0-1", 0, 1);
    DSDT_OF("cores-2-3", 2, 3);
    DSDT_OF("cores-0-1-2", 0, 1, 2);
    DSDT_OF("cores-10-35", 10, 35);
    DSDT_OF("cores-0-to-31", 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31);

    CheckAnsNodes();

    CheckCoprocessorNodes();
    if(argc == 5) {
        CheckRealTree(argv[3], argv[4]);
    }

    CheckWithoutSerialPort();

    CheckRefusals();

    return Verdict();
}
