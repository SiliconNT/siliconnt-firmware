/** @file
  Host test of what Library/AppleDTLib reads about the machine: AppleGetCpus(),
  AppleGetUartBase(), AppleGetMmioSpan() and AppleGetChipId().

  Builds Apple device trees in memory, the one of the M4 MacBook Pro among them,
  and checks what the functions make of them. Run it with ./run.sh; it needs the
  host compiler only.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "HostTest.h"

//
// A made-up machine with another shape: eight cores, two efficiency cores and
// two clusters of three performance cores, started on core 2. Its "ranges" are
// not in the order of their addresses and the serial port is in the second one.
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
    { 0x500000000ULL, 0x580000000ULL, 0x020000000ULL },
    { 0x000000000ULL, 0x280000000ULL, 0x100000000ULL },
    { 0x100000000ULL, 0x0c0000000ULL, 0x010000000ULL },
    { 0x700000000ULL, 0x700000000ULL, 0x000000000ULL },   // empty: does not count
};

STATIC CONST TEST_MACHINE mOther = {
    0x6000,
    mOtherCpus, ARRAY_SIZE(mOtherCpus),
    mOtherRanges, ARRAY_SIZE(mOtherRanges),
    { 0x039200000ULL, 0x4000 }
};

typedef struct
{
    UINT32          Number;
    UINT64          Mpidr;
    APPLE_CPU_CLASS Class;
    BOOLEAN         Boot;
} EXPECTED_CPU;

STATIC VOID CheckCpus(IN CONST CHAR8 *What, IN CONST TEST_MACHINE *Machine, IN CONST EXPECTED_CPU *Expected, IN UINTN ExpectedCount)
{
    APPLE_CPU     Cpus[APPLE_MAX_CPUS];
    UINTN         Count = 0x1111;
    RETURN_STATUS Status;
    BOOLEAN       Ok;
    UINTN         Index;

    BuildMachine(Machine);
    Ok = dt_check(mTree, mUsed, NULL) == 0;
    Status = AppleGetCpus(BuiltTree(), Cpus, &Count);
    Ok = Ok && Status == RETURN_SUCCESS && Count == ExpectedCount;
    for(Index = 0; Ok && Index < Count; Index++) {
        Ok = Cpus[Index].Number == Expected[Index].Number && Cpus[Index].Mpidr == Expected[Index].Mpidr &&
             Cpus[Index].Class == Expected[Index].Class && Cpus[Index].Boot == Expected[Index].Boot;
    }
    if(!Expect(Ok, What)) {
        printf("      status 0x%llx, %u core(s)\n", (unsigned long long)Status, (unsigned)Count);
        for(Index = 0; Status == RETURN_SUCCESS && Index < Count; Index++) {
            printf("      core %u mpidr 0x%llx class %d boot %d\n", (unsigned)Cpus[Index].Number, (unsigned long long)Cpus[Index].Mpidr, (int)Cpus[Index].Class, (int)Cpus[Index].Boot);
        }
    }
}

// The cores of a machine that is the M4 MacBook Pro except for Cpus
STATIC VOID CheckBadCpus(IN CONST CHAR8 *What, IN CONST TEST_CPU *TestCpus, IN UINTN TestCount, IN RETURN_STATUS Expected)
{
    TEST_MACHINE  Machine = mM4MacBookPro;
    APPLE_CPU     Cpus[APPLE_MAX_CPUS];
    UINTN         Count = 0x1111;
    RETURN_STATUS Status;

    Machine.Cpus     = TestCpus;
    Machine.CpuCount = TestCount;
    BuildMachine(&Machine);
    Status = AppleGetCpus(BuiltTree(), Cpus, &Count);
    if(!Expect(dt_check(mTree, mUsed, NULL) == 0 && Status == Expected && Count == 0, What)) {
        printf("      status 0x%llx, %u core(s)\n", (unsigned long long)Status, (unsigned)Count);
    }
}

#define BAD_CPUS(What, Expected, ...) \
    do { \
        STATIC CONST TEST_CPU List[] = { __VA_ARGS__ }; \
        CheckBadCpus(What, List, ARRAY_SIZE(List), Expected); \
    } while(0)

STATIC VOID CheckUart(IN CONST CHAR8 *What, IN RETURN_STATUS Expected, IN UINT64 ExpectedBase)
{
    UINT64        Base = 0x1111;
    RETURN_STATUS Status;

    Status = AppleGetUartBase(BuiltTree(), &Base);
    if(!Expect(dt_check(mTree, mUsed, NULL) == 0 && Status == Expected && Base == ExpectedBase, What)) {
        printf("      status 0x%llx base 0x%llx\n", (unsigned long long)Status, (unsigned long long)Base);
    }
}

STATIC VOID CheckSpan(IN CONST CHAR8 *What, IN RETURN_STATUS Expected, IN UINT64 ExpectedBase, IN UINT64 ExpectedSize)
{
    UINT64        Base = 0x1111;
    UINT64        Size = 0x2222;
    RETURN_STATUS Status;

    Status = AppleGetMmioSpan(BuiltTree(), &Base, &Size);
    if(!Expect(dt_check(mTree, mUsed, NULL) == 0 && Status == Expected && Base == ExpectedBase && Size == ExpectedSize, What)) {
        printf("      status 0x%llx base 0x%llx size 0x%llx\n", (unsigned long long)Status, (unsigned long long)Base, (unsigned long long)Size);
    }
}

STATIC VOID CheckChipId(IN CONST CHAR8 *What, IN RETURN_STATUS Expected, IN UINT32 ExpectedChipId)
{
    UINT32        ChipId = 0x1111;
    RETURN_STATUS Status;

    Status = AppleGetChipId(BuiltTree(), &ChipId);
    if(!Expect(dt_check(mTree, mUsed, NULL) == 0 && Status == Expected && ChipId == ExpectedChipId, What)) {
        printf("      status 0x%llx chip 0x%x\n", (unsigned long long)Status, (unsigned)ChipId);
    }
}

// The M4 MacBook Pro with other "ranges" and another "reg" of uart0
STATIC VOID BuildWithRanges(IN CONST UINT64 (*Ranges)[3], IN UINTN RangeCount, IN UINT64 UartAddress, IN UINT64 UartSize)
{
    TEST_MACHINE Machine = mM4MacBookPro;

    Machine.Ranges     = Ranges;
    Machine.RangeCount = RangeCount;
    Machine.UartReg[0] = UartAddress;
    Machine.UartReg[1] = UartSize;
    BuildMachine(&Machine);
}

//
// A tree written out by hand, for what BuildMachine() cannot express: the root
// and /arm-io with BusCells, CpuCells and SizeCells cells, "ranges" and "reg" of
// uart0 as given, and /chosen with a "chip-id" of ChipIdLength bytes. A count of
// 0 leaves the property out; so does a NULL.
//
STATIC VOID BuildByHand(IN UINT32 BusCells, IN UINT32 CpuCells, IN UINT32 SizeCells, IN CONST VOID *Ranges, IN UINT32 RangesLength, IN CONST VOID *UartReg, IN UINT32 UartRegLength, IN BOOLEAN WithUart, IN UINT32 ChipIdLength)
{
    STATIC CONST UINT8 ChipId[8] = { 0x32, 0x81, 0, 0, 0, 0, 0, 0 };

    mUsed = 0;
    Node(1 + (CpuCells != 0 ? 1 : 0), 2);
    Name("device-tree");
    if(CpuCells != 0) {
        Number("#address-cells", CpuCells);
    }
      Node(1 + (ChipIdLength != 0 ? 1 : 0), 0);
      Name("chosen");
      if(ChipIdLength != 0) {
          Prop("chip-id", ChipId, ChipIdLength);
      }
      Node(1 + (BusCells != 0 ? 1 : 0) + (SizeCells != 0 ? 1 : 0) + (Ranges != NULL ? 1 : 0), WithUart ? 1 : 0);
      Name("arm-io");
      if(BusCells != 0) {
          Number("#address-cells", BusCells);
      }
      if(SizeCells != 0) {
          Number("#size-cells", SizeCells);
      }
      if(Ranges != NULL) {
          Prop("ranges", Ranges, RangesLength);
      }
      if(WithUart) {
          Node(1 + (UartReg != NULL ? 1 : 0), 0);
          Name("uart0");
          if(UartReg != NULL) {
              Prop("reg", UartReg, UartRegLength);
          }
      }
}

int main(void)
{
    STATIC CONST EXPECTED_CPU M4Cpus[] = {
        { 0, 0x00000, AppleCpuEfficiency,  FALSE },
        { 1, 0x00001, AppleCpuEfficiency,  FALSE },
        { 2, 0x00002, AppleCpuEfficiency,  FALSE },
        { 3, 0x00003, AppleCpuEfficiency,  FALSE },
        { 4, 0x00004, AppleCpuEfficiency,  FALSE },
        { 5, 0x00005, AppleCpuEfficiency,  FALSE },
        { 6, 0x10100, AppleCpuPerformance, TRUE  },
        { 7, 0x10101, AppleCpuPerformance, FALSE },
        { 8, 0x10102, AppleCpuPerformance, FALSE },
        { 9, 0x10103, AppleCpuPerformance, FALSE },
    };
    STATIC CONST EXPECTED_CPU OtherCpus[] = {
        { 0, 0x00000, AppleCpuEfficiency,  FALSE },
        { 1, 0x00001, AppleCpuEfficiency,  FALSE },
        { 2, 0x10100, AppleCpuPerformance, TRUE  },
        { 3, 0x10101, AppleCpuPerformance, FALSE },
        { 4, 0x10102, AppleCpuPerformance, FALSE },
        { 5, 0x10200, AppleCpuPerformance, FALSE },
        { 6, 0x10201, AppleCpuPerformance, FALSE },
        { 7, 0x10202, AppleCpuPerformance, FALSE },
    };
    // 32-bit cells: bus 0x1000_0000 is at 0x9000_0000 for the CPU, 256 MB long
    STATIC CONST UINT32 Ranges32[]   = { 0x10000000, 0x90000000, 0x10000000 };
    STATIC CONST UINT32 Reg32[]      = { 0x10200000, 0x4000 };
    // The same bus with 64-bit addresses for the CPU
    STATIC CONST UINT32 RangesMixed[] = { 0x10000000, 0x90000000, 0x00000002, 0x10000000 };
    STATIC CONST UINT64 Reg64[]      = { 0x1ad200000ULL, 0x4000 };
    STATIC CONST UINT64 OneRange[][3] = { { 0x100000000ULL, 0x300000000ULL, 0x100000000ULL } };
    STATIC CONST UINT64 Wrapping[][3] = { { 0, 0x200000000ULL, 0x100000000ULL }, { 0, 0xffffffff00000000ULL, 0x100000000ULL } };
    STATIC CONST UINT64 ToTheTop[][3] = { { 0, 0xffffffff00000000ULL, 0x0ffffffffULL } };
    STATIC CONST UINT64 AllEmpty[][3] = { { 0, 0x200000000ULL, 0 } };

    //
    // Cores
    //
    CheckCpus("M4 MacBook Pro: ten cores, numbers, MPIDR, classes, boot core 6", &mM4MacBookPro, M4Cpus, ARRAY_SIZE(M4Cpus));
    CheckCpus("M4 MacBook Pro with core 6 alone in the tree", &mM4MacBookProOneCore, &M4Cpus[6], 1);
    CheckCpus("eight cores, two performance clusters, boot core 2", &mOther, OtherCpus, ARRAY_SIZE(OtherCpus));
    {
        STATIC CONST TEST_CPU     Cpus[]     = { { 31, 0x7ff, "P", "running" }, { 0, 0, "E", "waiting" } };
        STATIC CONST EXPECTED_CPU Expected[] = { { 31, 0x107ff, AppleCpuPerformance, TRUE }, { 0, 0, AppleCpuEfficiency, FALSE } };
        TEST_MACHINE              Machine    = mM4MacBookPro;

        Machine.Cpus     = Cpus;
        Machine.CpuCount = ARRAY_SIZE(Cpus);
        CheckCpus("core 31 and a \"reg\" of 0x7ff are taken; the order is the tree's", &Machine, Expected, ARRAY_SIZE(Expected));
    }

    BAD_CPUS("no core is running", RETURN_INVALID_PARAMETER, { 0, 0, "E", "waiting" }, { 1, 1, "E", "waiting" });
    BAD_CPUS("two cores are running", RETURN_INVALID_PARAMETER, { 0, 0, "E", "running" }, { 1, 1, "E", "running" });
    BAD_CPUS("a core without \"state\"", RETURN_INVALID_PARAMETER, { 0, 0, "E", "running" }, { 1, 1, "E", NULL });
    BAD_CPUS("a core without \"cluster-type\"", RETURN_INVALID_PARAMETER, { 0, 0, "E", "running" }, { 1, 1, NULL, "waiting" });
    BAD_CPUS("a core of class \"X\"", RETURN_INVALID_PARAMETER, { 0, 0, "X", "running" });
    BAD_CPUS("a core of class \"PE\"", RETURN_INVALID_PARAMETER, { 0, 0, "PE", "running" });
    BAD_CPUS("a core of class \"\"", RETURN_INVALID_PARAMETER, { 0, 0, "", "running" });
    BAD_CPUS("a core number of 32", RETURN_INVALID_PARAMETER, { 0, 0, "E", "running" }, { 32, 1, "E", "waiting" });
    BAD_CPUS("two cores with the same number", RETURN_INVALID_PARAMETER, { 3, 0, "E", "running" }, { 3, 1, "E", "waiting" });
    BAD_CPUS("a \"reg\" with a die number", RETURN_INVALID_PARAMETER, { 0, 0, "E", "running" }, { 1, 0x800, "E", "waiting" });
    {
        TEST_MACHINE  Machine = mM4MacBookPro;
        APPLE_CPU     Cpus[APPLE_MAX_CPUS];
        UINTN         Count = 0x1111;
        RETURN_STATUS Status;

        Machine.CpuCount = 0;
        BuildMachine(&Machine);
        Status = AppleGetCpus(BuiltTree(), Cpus, &Count);
        Expect(Status == RETURN_NOT_FOUND && Count == 0, "/cpus without a core");

        BuildByHand(2, 2, 2, mRangesOfM4MacBookPro, sizeof(mRangesOfM4MacBookPro), Reg64, sizeof(Reg64), TRUE, 4);
        Count  = 0x1111;
        Status = AppleGetCpus(BuiltTree(), Cpus, &Count);
        Expect(Status == RETURN_NOT_FOUND && Count == 0, "no /cpus");
    }

    //
    // Serial port
    //
    BuildMachine(&mM4MacBookPro);
    CheckUart("M4 MacBook Pro: uart0 at 0x3_ad20_0000", RETURN_SUCCESS, 0x3ad200000ULL);
    BuildMachine(&mM4MacBookProOneCore);
    CheckUart("the same on one core", RETURN_SUCCESS, 0x3ad200000ULL);
    BuildMachine(&mOther);
    CheckUart("a range that is not the first one", RETURN_SUCCESS, 0x2b9200000ULL);
    BuildWithRanges(OneRange, 1, 0x1ffffc000ULL, 0x4000);
    CheckUart("registers that end where the range ends", RETURN_SUCCESS, 0x3ffffc000ULL);
    BuildWithRanges(OneRange, 1, 0x1ffffc000ULL, 0x8000);
    CheckUart("registers that reach beyond the range stay as they are", RETURN_SUCCESS, 0x1ffffc000ULL);
    BuildWithRanges(OneRange, 1, 0x0ad200000ULL, 0x4000);
    CheckUart("an address below every range stays as it is", RETURN_SUCCESS, 0x0ad200000ULL);
    BuildWithRanges(NULL, 0, 0x1ad200000ULL, 0x4000);
    CheckUart("empty \"ranges\": the address stays as it is", RETURN_SUCCESS, 0x1ad200000ULL);
    BuildWithRanges(NULL, 0, 0, 0x4000);
    CheckUart("an address of 0, which stands for no port, is a fault", RETURN_INVALID_PARAMETER, 0);
    BuildByHand(1, 1, 1, Ranges32, sizeof(Ranges32), Reg32, sizeof(Reg32), TRUE, 4);
    CheckUart("32-bit cells everywhere", RETURN_SUCCESS, 0x90200000ULL);
    BuildByHand(1, 2, 1, RangesMixed, sizeof(RangesMixed), Reg32, sizeof(Reg32), TRUE, 4);
    CheckUart("32-bit bus, 64-bit addresses for the CPU", RETURN_SUCCESS, 0x290200000ULL);
    BuildByHand(2, 2, 2, mRangesOfM4MacBookPro, sizeof(mRangesOfM4MacBookPro), Reg64, sizeof(Reg64), FALSE, 4);
    CheckUart("no uart0: no serial port", RETURN_NOT_FOUND, 0);
    {
        TEST_MACHINE Machine = mM4MacBookPro;

        // What m1n1 hands over when no host is attached hides the port and nothing else
        Machine.Uart = TestUartNameCleared;
        BuildMachine(&Machine);
        CheckUart("M4 MacBook Pro with the name of uart0 cleared, as m1n1 hands it over without a host: no serial port", RETURN_NOT_FOUND, 0);
        CheckCpus("the same tree: the cores as with the name", &Machine, M4Cpus, ARRAY_SIZE(M4Cpus));
        CheckSpan("the same tree: the device registers as with the name", RETURN_SUCCESS, 0x200000000ULL, 0x1cc0000000ULL - 0x200000000ULL);
        CheckChipId("the same tree: the chip as with the name", RETURN_SUCCESS, 0x8132);

        Machine.Uart = TestUartLeftOut;
        BuildMachine(&Machine);
        CheckUart("M4 MacBook Pro without uart0: no serial port", RETURN_NOT_FOUND, 0);
    }
    // uart0 is there and its address cannot be worked out: a fault of the tree, not a machine without a port
    BuildByHand(2, 2, 2, mRangesOfM4MacBookPro, sizeof(mRangesOfM4MacBookPro), NULL, 0, TRUE, 4);
    CheckUart("uart0 without \"reg\"", RETURN_INVALID_PARAMETER, 0);
    BuildByHand(2, 2, 2, mRangesOfM4MacBookPro, sizeof(mRangesOfM4MacBookPro), Reg64, 12, TRUE, 4);
    CheckUart("a \"reg\" of 12 bytes", RETURN_INVALID_PARAMETER, 0);
    BuildByHand(2, 2, 2, NULL, 0, Reg64, sizeof(Reg64), TRUE, 4);
    CheckUart("/arm-io without \"ranges\"", RETURN_INVALID_PARAMETER, 0);
    BuildByHand(2, 2, 2, mRangesOfM4MacBookPro, 100, Reg64, sizeof(Reg64), TRUE, 4);
    CheckUart("\"ranges\" that is not a whole number of entries", RETURN_INVALID_PARAMETER, 0);
    BuildByHand(0, 2, 2, mRangesOfM4MacBookPro, sizeof(mRangesOfM4MacBookPro), Reg64, sizeof(Reg64), TRUE, 4);
    CheckUart("/arm-io without \"#address-cells\"", RETURN_INVALID_PARAMETER, 0);
    BuildByHand(2, 0, 2, mRangesOfM4MacBookPro, sizeof(mRangesOfM4MacBookPro), Reg64, sizeof(Reg64), TRUE, 4);
    CheckUart("root without \"#address-cells\"", RETURN_INVALID_PARAMETER, 0);
    BuildByHand(2, 2, 0, mRangesOfM4MacBookPro, sizeof(mRangesOfM4MacBookPro), Reg64, sizeof(Reg64), TRUE, 4);
    CheckUart("/arm-io without \"#size-cells\"", RETURN_INVALID_PARAMETER, 0);
    BuildByHand(3, 2, 2, mRangesOfM4MacBookPro, sizeof(mRangesOfM4MacBookPro), Reg64, sizeof(Reg64), TRUE, 4);
    CheckUart("three address cells", RETURN_INVALID_PARAMETER, 0);
    {
        // /chosen only
        mUsed = 0;
        Node(2, 1);
        Name("device-tree");
        Number("#address-cells", 2);
          Node(2, 0);
          Name("chosen");
          Number("chip-id", 0x8132);
        CheckUart("no /arm-io: no uart0, so no serial port", RETURN_NOT_FOUND, 0);
        CheckSpan("no /arm-io: no device registers either", RETURN_NOT_FOUND, 0, 0);
    }

    //
    // Device registers
    //
    BuildMachine(&mM4MacBookPro);
    CheckSpan("M4 MacBook Pro: 0x2_0000_0000 .. 0x1c_c000_0000", RETURN_SUCCESS, 0x200000000ULL, 0x1cc0000000ULL - 0x200000000ULL);
    BuildMachine(&mOther);
    CheckSpan("lowest and highest range in any order; an empty range does not count", RETURN_SUCCESS, 0x0c0000000ULL, 0x5a0000000ULL - 0x0c0000000ULL);
    BuildWithRanges(OneRange, 1, 0x1ad200000ULL, 0x4000);
    CheckSpan("one range", RETURN_SUCCESS, 0x300000000ULL, 0x100000000ULL);
    BuildWithRanges(ToTheTop, 1, 0x1ad200000ULL, 0x4000);
    CheckSpan("a range that ends one byte below the top of the address space", RETURN_SUCCESS, 0xffffffff00000000ULL, 0x0ffffffffULL);
    BuildWithRanges(Wrapping, 2, 0x1ad200000ULL, 0x4000);
    CheckSpan("a range that wraps around", RETURN_INVALID_PARAMETER, 0, 0);
    BuildWithRanges(AllEmpty, 1, 0x1ad200000ULL, 0x4000);
    CheckSpan("only an empty range", RETURN_NOT_FOUND, 0, 0);
    BuildWithRanges(NULL, 0, 0x1ad200000ULL, 0x4000);
    CheckSpan("empty \"ranges\"", RETURN_NOT_FOUND, 0, 0);
    BuildByHand(1, 2, 1, RangesMixed, sizeof(RangesMixed), Reg32, sizeof(Reg32), TRUE, 4);
    CheckSpan("32-bit bus, 64-bit addresses for the CPU", RETURN_SUCCESS, 0x290000000ULL, 0x10000000ULL);

    //
    // Chip number
    //
    BuildMachine(&mM4MacBookPro);
    CheckChipId("M4 MacBook Pro: 0x8132", RETURN_SUCCESS, 0x8132);
    BuildMachine(&mOther);
    CheckChipId("another chip", RETURN_SUCCESS, 0x6000);
    BuildByHand(2, 2, 2, mRangesOfM4MacBookPro, sizeof(mRangesOfM4MacBookPro), Reg64, sizeof(Reg64), TRUE, 0);
    CheckChipId("/chosen without \"chip-id\"", RETURN_NOT_FOUND, 0);
    BuildByHand(2, 2, 2, mRangesOfM4MacBookPro, sizeof(mRangesOfM4MacBookPro), Reg64, sizeof(Reg64), TRUE, 8);
    CheckChipId("a \"chip-id\" of 8 bytes", RETURN_INVALID_PARAMETER, 0);
    {
        mUsed = 0;
        Node(1, 0);
        Name("device-tree");
        CheckChipId("no /chosen", RETURN_NOT_FOUND, 0);
    }

    return Verdict();
}
