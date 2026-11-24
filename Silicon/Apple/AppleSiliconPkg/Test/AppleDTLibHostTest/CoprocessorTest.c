/** @file
  Host test of AppleGetMtp() and AppleGetSmc() in Library/AppleDTLib: what the
  firmware reads about the registers and the interrupt lines of two coprocessors,
  MTP (the keyboard and the trackpad) and SMC.

  Builds Apple device trees in memory, the one of the M4 MacBook Pro among them
  (CoprocessorTree.h), and checks what the functions make of them. Run it with
  ./run.sh; it needs the host compiler only.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "CoprocessorTree.h"

#include <stdlib.h>

typedef RETURN_STATUS (*GET_DEVICE)(IN CONST APPLE_BOOT_INFO *Info, OUT APPLE_DEVICE *Device);

//
// A device as text: one line for a range, " (power manager)" after one it shares,
// then one for an interrupt line, then the status of the SRAM
//
STATIC CONST CHAR8 *Dump(IN CONST APPLE_DEVICE *Device)
{
    STATIC CHAR8 Text[8192];
    UINTN        Used = 0;
    UINTN        Index;

    Text[0] = 0;
    for(Index = 0; Index < Device->RangeCount; Index++) {
        CONST APPLE_DEVICE_RANGE *Range = &Device->Ranges[Index];

        Used += snprintf(Text + Used, sizeof(Text) - Used, "%s", Range->Block);
        if(Range->Entry != APPLE_DEVICE_NO_ENTRY) {
            Used += snprintf(Text + Used, sizeof(Text) - Used, "-%u", (unsigned)Range->Entry);
        }
        Used += snprintf(Text + Used, sizeof(Text) - Used, " %llx+%llx%s\n", (unsigned long long)Range->Range.Base,
                         (unsigned long long)Range->Range.Size, Range->PowerManager ? " (power manager)" : "");
    }
    for(Index = 0; Index < Device->LineCount; Index++) {
        Used += snprintf(Text + Used, sizeof(Text) - Used, "%s-%u AIC %u\n", Device->Lines[Index].Block, (unsigned)Device->Lines[Index].Entry,
                         (unsigned)Device->Lines[Index].Aic);
    }
    snprintf(Text + Used, sizeof(Text) - Used, "SRAM 0x%llx\n", (unsigned long long)Device->SramStatus);
    return Text;
}

//
// What AppleGetMtp() hands back for the M4 MacBook Pro, in pieces: the "reg"
// entries, moved by 0x2_0000_0000 (the first range of /arm-io), region-id-91, the
// AIC numbers (SiliconNT docs/findings/0095, conclusion 2)
//
#define M4_MTP_RANGES   "mtp-0 394600000+88000\n" "mtp-1 394050000+4000\n"
#define M4_DART_RANGES  "dart-mtp-0 394800000+c000\n" "dart-mtp-1 394810000+4000\n"
#define M4_PS_PROT      "dart-mtp-2 3882a4000+4000 (power manager)\n"
#define M4_DOCK_RANGES  "dockchannel-mtp-0 394b00000+1000\n" "dockchannel-mtp-1 394b14000+1000\n" "dockchannel-mtp-2 394b30000+1000\n" \
                        "dockchannel-mtp-3 394b34000+1000\n" "dockchannel-mtp-4 394b28000+1000\n" "dockchannel-mtp-5 394b2c000+1000\n"
#define M4_MTP_SRAM     "mtp-sram 394c00000+140000\n"
#define M4_MTP_LINES    "mtp-0 AIC 1114\n" "mtp-1 AIC 1113\n" "mtp-2 AIC 1116\n" "mtp-3 AIC 1115\n"
#define M4_OTHER_LINES  "dart-mtp-0 AIC 1091\n" "dockchannel-mtp-0 AIC 1093\n"
#define SRAM_FOUND      "SRAM 0x0\n"
#define SRAM_NOT_FOUND  "SRAM 0x800000000000000e\n"
#define SRAM_INVALID    "SRAM 0x8000000000000002\n"

#define M4_MTP  M4_MTP_RANGES M4_DART_RANGES M4_PS_PROT M4_DOCK_RANGES M4_MTP_SRAM M4_MTP_LINES M4_OTHER_LINES SRAM_FOUND
#define M4_MTP_WITHOUT_SRAM  M4_MTP_RANGES M4_DART_RANGES M4_PS_PROT M4_DOCK_RANGES M4_MTP_LINES M4_OTHER_LINES

#define M4_SMC_RANGES  "smc-0 38c600000+88000\n" "smc-1 38c050000+4000\n" "smc-2 38c810000+100\n"
#define M4_SMC_LINES   "smc-0 AIC 562\n" "smc-1 AIC 561\n" "smc-2 AIC 564\n" "smc-3 AIC 563\n"
#define M4_SMC         M4_SMC_RANGES "smc-region 38de00000+100000\n" M4_SMC_LINES SRAM_FOUND

STATIC VOID CheckMachine(IN CONST CHAR8 *What, IN CONST TEST_MACHINE *Machine, IN GET_DEVICE Get, IN CONST CHAR8 *Expected)
{
    APPLE_DEVICE  Device;
    RETURN_STATUS Status;

    BuildMachine(Machine);
    memset(&Device, 0x5a, sizeof(Device));
    Status = Get(BuiltTree(), &Device);
    if(!Expect(dt_check(mTree, mUsed, NULL) == 0 && Status == RETURN_SUCCESS && Device.FailedNode == NULL && Device.FailedProperty == NULL &&
               strcmp(Dump(&Device), Expected) == 0, What)) {
        printf("      status 0x%llx, node %s, property %s\n%s", (unsigned long long)Status, Device.FailedNode != NULL ? Device.FailedNode : "-",
               Device.FailedProperty != NULL ? Device.FailedProperty : "-", RETURN_ERROR(Status) ? "" : Dump(&Device));
    }
}

// The M4 MacBook Pro with Edits
STATIC VOID Check(IN CONST CHAR8 *What, IN CONST TEST_EDIT *Edits, IN UINTN EditCount, IN GET_DEVICE Get, IN CONST CHAR8 *Expected)
{
    TEST_MACHINE Machine = mM4MacBookProWhole;

    Machine.Edits     = Edits;
    Machine.EditCount = EditCount;
    CheckMachine(What, &Machine, Get, Expected);
}

//
// Nothing is read: Status, what it was that could not be read, and nothing else
// handed back
//
STATIC VOID CheckRefused(IN CONST CHAR8 *What, IN CONST TEST_EDIT *Edits, IN UINTN EditCount, IN GET_DEVICE Get, IN RETURN_STATUS Expected, IN CONST CHAR8 *FailedNode, IN CONST CHAR8 *FailedProperty)
{
    STATIC APPLE_DEVICE Expectation;
    APPLE_DEVICE        Device;
    RETURN_STATUS       Status;
    TEST_MACHINE        Machine = mM4MacBookProWhole;
    BOOLEAN             Ok;

    Machine.Edits     = Edits;
    Machine.EditCount = EditCount;
    BuildMachine(&Machine);
    memset(&Device, 0x5a, sizeof(Device));
    Status = Get(BuiltTree(), &Device);
    Ok     = dt_check(mTree, mUsed, NULL) == 0 && Status == Expected &&
             Device.FailedNode != NULL && strcmp(Device.FailedNode, FailedNode) == 0 &&
             (FailedProperty == NULL ? Device.FailedProperty == NULL : Device.FailedProperty != NULL && strcmp(Device.FailedProperty, FailedProperty) == 0);
    if(!Ok) {
        printf("      status 0x%llx, node %s, property %s\n", (unsigned long long)Status, Device.FailedNode != NULL ? Device.FailedNode : "-",
               Device.FailedProperty != NULL ? Device.FailedProperty : "-");
    }
    Expectation.FailedNode     = Device.FailedNode;
    Expectation.FailedProperty = Device.FailedProperty;
    Expect(Ok && memcmp(&Device, &Expectation, sizeof(Device)) == 0, What);
}

#define CHECK(What, Get, Expected, ...) \
    do { \
        CONST TEST_EDIT Edits[] = { __VA_ARGS__ }; \
        Check(What, Edits, ARRAY_SIZE(Edits), Get, Expected); \
    } while(0)

#define REFUSED(What, Get, Status, FailedNode, FailedProperty, ...) \
    do { \
        CONST TEST_EDIT Edits[] = { __VA_ARGS__ }; \
        CheckRefused(What, Edits, ARRAY_SIZE(Edits), Get, Status, FailedNode, FailedProperty); \
    } while(0)

STATIC VOID CheckMtp(VOID)
{
    TEST_MACHINE Machine;
    TEST_PMGR    Pmgr;

    Check("MTP of the M4 MacBook Pro", NULL, 0, AppleGetMtp, M4_MTP);

    //
    // Another shape of "reg" and "interrupts"
    //
    CHECK("dart-mtp with two ranges, no PS_PROT", AppleGetMtp,
          M4_MTP_RANGES M4_DART_RANGES M4_DOCK_RANGES M4_MTP_SRAM M4_MTP_LINES M4_OTHER_LINES SRAM_FOUND,
          EDIT_WORDS("dart-mtp", "reg", 0x94800000, 1, 0xc000, 0, 0x94810000, 1, 0x4000, 0));
    CHECK("dart-mtp with one range", AppleGetMtp,
          M4_MTP_RANGES "dart-mtp-0 394808000+4000\n" M4_DOCK_RANGES M4_MTP_SRAM M4_MTP_LINES M4_OTHER_LINES SRAM_FOUND,
          EDIT_WORDS("dart-mtp", "reg", 0x94808000, 1, 0x4000, 0));
    CHECK("dart-mtp with two lines, mtp with one, dockchannel-mtp with one range", AppleGetMtp,
          M4_MTP_RANGES M4_DART_RANGES M4_PS_PROT "dockchannel-mtp-0 394b00000+1000\n" M4_MTP_SRAM
          "mtp-0 AIC 7\n" "dart-mtp-0 AIC 9\n" "dart-mtp-1 AIC 8\n" "dockchannel-mtp-0 AIC 1093\n" SRAM_FOUND,
          EDIT_WORDS("mtp", "interrupts", 7), EDIT_WORDS("dart-mtp", "interrupts", 9, 8), EDIT_WORDS("dockchannel-mtp", "reg", 0x94b00000, 1, 0x1000, 0));
    CHECK("a range at the top of the address space, not inside any of \"ranges\"", AppleGetMtp,
          "mtp-0 fffffffffffff000+1000\n" M4_DART_RANGES M4_PS_PROT M4_DOCK_RANGES M4_MTP_SRAM M4_MTP_LINES M4_OTHER_LINES SRAM_FOUND,
          EDIT_WORDS("mtp", "reg", 0xfffff000, 0xffffffff, 0x1000, 0));

    //
    // The power manager: a range it shares is marked, wherever it is
    //
    Machine      = mM4MacBookProWhole;
    Machine.Pmgr = NULL;
    CheckMachine("no /arm-io/pmgr: dart-mtp-2 is the device's own", &Machine, AppleGetMtp,
                 M4_MTP_RANGES M4_DART_RANGES "dart-mtp-2 3882a4000+4000\n" M4_DOCK_RANGES M4_MTP_SRAM M4_MTP_LINES M4_OTHER_LINES SRAM_FOUND);

    Pmgr         = mPmgrOfM4MacBookPro;
    Pmgr.Omit    = OMIT_PMGR_REG;
    Machine.Pmgr = &Pmgr;
    CheckMachine("/arm-io/pmgr without \"reg\": dart-mtp-2 is the device's own", &Machine, AppleGetMtp,
                 M4_MTP_RANGES M4_DART_RANGES "dart-mtp-2 3882a4000+4000\n" M4_DOCK_RANGES M4_MTP_SRAM M4_MTP_LINES M4_OTHER_LINES SRAM_FOUND);
    Pmgr.Omit = 0;
    {
        STATIC CONST UINT64 Elsewhere[][2] = { { 0x180700000ULL, 0xcc000 }, { 0x188300000ULL, 0x80000 } };
        STATIC CONST UINT64 Below[][2]     = { { 0x188280000ULL, 0x24000 }, { 0x1882a4000ULL, 0 } };
        STATIC CONST UINT64 Above[][2]     = { { 0x1882a8000ULL, 0x1000 } };
        STATIC CONST UINT64 LastByte[][2]  = { { 0x1882a7fffULL, 1 } };
        STATIC CONST UINT64 FirstByte[][2] = { { 0x180000000ULL, 0x1000 }, { 0x188280000ULL, 0x24001 } };
        STATIC CONST UINT64 Inside[][2]    = { { 0x1882a4100ULL, 4 } };
        STATIC CONST UINT64 All[][2]       = { { 0x100000000ULL, 0x100000000ULL } };
        struct {
            CONST CHAR8  *What;
            CONST UINT64 (*Reg)[2];
            UINT32       Length;
            CONST CHAR8  *Expected;
        } Cases[] = {
            { "the power manager elsewhere: dart-mtp-2 is the device's own", Elsewhere, sizeof(Elsewhere),
              M4_MTP_RANGES M4_DART_RANGES "dart-mtp-2 3882a4000+4000\n" M4_DOCK_RANGES M4_MTP_SRAM M4_MTP_LINES M4_OTHER_LINES SRAM_FOUND },
            { "the power manager ends where dart-mtp-2 starts, another entry is empty: the device's own", Below, sizeof(Below),
              M4_MTP_RANGES M4_DART_RANGES "dart-mtp-2 3882a4000+4000\n" M4_DOCK_RANGES M4_MTP_SRAM M4_MTP_LINES M4_OTHER_LINES SRAM_FOUND },
            { "the power manager starts where dart-mtp-2 ends: the device's own", Above, sizeof(Above),
              M4_MTP_RANGES M4_DART_RANGES "dart-mtp-2 3882a4000+4000\n" M4_DOCK_RANGES M4_MTP_SRAM M4_MTP_LINES M4_OTHER_LINES SRAM_FOUND },
            { "the power manager has the last byte of dart-mtp-2", LastByte, sizeof(LastByte), M4_MTP },
            { "the power manager's second entry has the first byte of dart-mtp-2", FirstByte, sizeof(FirstByte), M4_MTP },
            { "the power manager lies inside dart-mtp-2", Inside, sizeof(Inside), M4_MTP },
            { "the power manager shares everything, the SRAM too", All, sizeof(All),
              "mtp-0 394600000+88000 (power manager)\n" "mtp-1 394050000+4000 (power manager)\n" "dart-mtp-0 394800000+c000 (power manager)\n"
              "dart-mtp-1 394810000+4000 (power manager)\n" M4_PS_PROT "dockchannel-mtp-0 394b00000+1000 (power manager)\n"
              "dockchannel-mtp-1 394b14000+1000 (power manager)\n" "dockchannel-mtp-2 394b30000+1000 (power manager)\n"
              "dockchannel-mtp-3 394b34000+1000 (power manager)\n" "dockchannel-mtp-4 394b28000+1000 (power manager)\n"
              "dockchannel-mtp-5 394b2c000+1000 (power manager)\n" "mtp-sram 394c00000+140000 (power manager)\n" M4_MTP_LINES M4_OTHER_LINES SRAM_FOUND },
        };
        UINTN Index;

        for(Index = 0; Index < ARRAY_SIZE(Cases); Index++) {
            Pmgr.Reg       = Cases[Index].Reg;
            Pmgr.RegLength = Cases[Index].Length;
            CheckMachine(Cases[Index].What, &Machine, AppleGetMtp, Cases[Index].Expected);
        }
        Pmgr.Reg          = Elsewhere;
        Pmgr.RegLength    = sizeof(Elsewhere);
        Machine.PmgrChild = &mPmgrOfM4MacBookPro;
        CheckMachine("/arm-io/pmgr elsewhere, /arm-io/pmgr-child has dart-mtp-2", &Machine, AppleGetMtp, M4_MTP);
    }

    //
    // The SRAM
    //
    CHECK("no segment-ranges: no SRAM", AppleGetMtp, M4_MTP_WITHOUT_SRAM SRAM_NOT_FOUND, EDIT_NO_PROPERTY("mtp", "segment-ranges"));
    CHECK("segment-ranges empty: no SRAM", AppleGetMtp, M4_MTP_WITHOUT_SRAM SRAM_INVALID, EDIT_BYTES("mtp", "segment-ranges", ""));
    CHECK("segment-ranges of one segment and a half: no SRAM", AppleGetMtp, M4_MTP_WITHOUT_SRAM SRAM_INVALID,
          EDIT_WORDS("mtp", "segment-ranges", 0x94c00000, 3, 0, 0, 0, 0, 0x1000, 0, 0x94c00000, 3));
    CHECK("no carveout-memory-map: no SRAM", AppleGetMtp, M4_MTP_WITHOUT_SRAM SRAM_NOT_FOUND, EDIT_NO_NODE("carveout-memory-map"));
    CHECK("no region for the segment: no SRAM", AppleGetMtp, M4_MTP_WITHOUT_SRAM SRAM_NOT_FOUND, EDIT_NO_PROPERTY("carveout-memory-map", "region-id-91"));
    CHECK("the region ends where the segment starts: no SRAM", AppleGetMtp, M4_MTP_WITHOUT_SRAM SRAM_NOT_FOUND,
          EDIT_WORDS("carveout-memory-map", "region-id-91", 0x94b00000, 3, 0x100000, 0));
    CHECK("the region starts right after where the segment does: no SRAM", AppleGetMtp, M4_MTP_WITHOUT_SRAM SRAM_NOT_FOUND,
          EDIT_WORDS("carveout-memory-map", "region-id-91", 0x94c00001, 3, 0x1000, 0));
    CHECK("the region's last byte is where the segment starts", AppleGetMtp,
          M4_MTP_RANGES M4_DART_RANGES M4_PS_PROT M4_DOCK_RANGES "mtp-sram 394b00000+100001\n" M4_MTP_LINES M4_OTHER_LINES SRAM_FOUND,
          EDIT_WORDS("carveout-memory-map", "region-id-91", 0x94b00000, 3, 0x100001, 0));
    CHECK("the region wraps around the address space: no SRAM", AppleGetMtp, M4_MTP_WITHOUT_SRAM SRAM_NOT_FOUND,
          EDIT_WORDS("carveout-memory-map", "region-id-91", 0x94c00000, 3, 0xffffffff, 0xffffffff));
    CHECK("a region of 8 bytes is no region", AppleGetMtp, M4_MTP_WITHOUT_SRAM SRAM_NOT_FOUND,
          EDIT_WORDS("carveout-memory-map", "region-id-91", 0x94c00000, 3));
    CHECK("a region of 24 bytes is no region", AppleGetMtp, M4_MTP_WITHOUT_SRAM SRAM_NOT_FOUND,
          EDIT_WORDS("carveout-memory-map", "region-id-91", 0x94c00000, 3, 0x140000, 0, 0, 0));
    CHECK("a property that is not called region-id-... is no region", AppleGetMtp, M4_MTP_WITHOUT_SRAM SRAM_NOT_FOUND,
          EDIT_NO_PROPERTY("carveout-memory-map", "region-id-91"), EDIT_WORDS("carveout-memory-map", "region-name-id-91", 0x94c00000, 3, 0x140000, 0));
    CHECK("the segment is in DRAM, so is the region: no SRAM", AppleGetMtp, M4_MTP_WITHOUT_SRAM SRAM_NOT_FOUND,
          EDIT_WORDS("mtp", "segment-ranges", 0x10000000, 0x100, 0, 0, 0, 0, 0x1000, 0));
    CHECK("the first region that holds the segment reaches into DRAM, the second is the SRAM", AppleGetMtp,
          M4_MTP,
          EDIT_WORDS("carveout-memory-map", "region-id-24", 0x94000000, 3, 0x6c000001, 0xfc));
    CHECK("where the DRAM is, is not known: no SRAM", AppleGetMtp, M4_MTP_WITHOUT_SRAM SRAM_NOT_FOUND, EDIT_NO_PROPERTY(NULL, "dram-base"));
    CHECK("how long the DRAM is, is not known: no SRAM", AppleGetMtp, M4_MTP_WITHOUT_SRAM SRAM_NOT_FOUND, EDIT_NO_PROPERTY(NULL, "dram-size"));
    CHECK("dram-base of 4 bytes: no SRAM", AppleGetMtp, M4_MTP_WITHOUT_SRAM SRAM_INVALID, EDIT_WORDS(NULL, "dram-base", 0));

    //
    // What has to be there
    //
    REFUSED("no mtp", AppleGetMtp, RETURN_NOT_FOUND, "/arm-io/mtp", NULL, EDIT_NO_NODE("mtp"));
    REFUSED("no dart-mtp", AppleGetMtp, RETURN_NOT_FOUND, "/arm-io/dart-mtp", NULL, EDIT_NO_NODE("dart-mtp"));
    REFUSED("no dockchannel-mtp", AppleGetMtp, RETURN_NOT_FOUND, "/arm-io/dockchannel-mtp", NULL, EDIT_NO_NODE("dockchannel-mtp"));
    REFUSED("mtp without \"reg\"", AppleGetMtp, RETURN_NOT_FOUND, "/arm-io/mtp", "reg", EDIT_NO_PROPERTY("mtp", "reg"));
    REFUSED("dart-mtp without \"reg\" (its mapper has one)", AppleGetMtp, RETURN_NOT_FOUND, "/arm-io/dart-mtp", "reg", EDIT_NO_PROPERTY("dart-mtp", "reg"));
    REFUSED("dockchannel-mtp without \"reg\"", AppleGetMtp, RETURN_NOT_FOUND, "/arm-io/dockchannel-mtp", "reg", EDIT_NO_PROPERTY("dockchannel-mtp", "reg"));
    REFUSED("mtp without \"interrupts\"", AppleGetMtp, RETURN_NOT_FOUND, "/arm-io/mtp", "interrupts", EDIT_NO_PROPERTY("mtp", "interrupts"));
    REFUSED("dart-mtp without \"interrupts\"", AppleGetMtp, RETURN_NOT_FOUND, "/arm-io/dart-mtp", "interrupts", EDIT_NO_PROPERTY("dart-mtp", "interrupts"));
    REFUSED("dockchannel-mtp without \"interrupts\"", AppleGetMtp, RETURN_NOT_FOUND, "/arm-io/dockchannel-mtp", "interrupts", EDIT_NO_PROPERTY("dockchannel-mtp", "interrupts"));
    REFUSED("\"reg\" empty", AppleGetMtp, RETURN_INVALID_PARAMETER, "/arm-io/mtp", "reg", EDIT_BYTES("mtp", "reg", ""));
    REFUSED("\"reg\" of one entry and a half", AppleGetMtp, RETURN_INVALID_PARAMETER, "/arm-io/mtp", "reg",
            EDIT_WORDS("mtp", "reg", 0x94600000, 1, 0x88000, 0, 0x94050000, 1));
    REFUSED("an entry of size 0", AppleGetMtp, RETURN_INVALID_PARAMETER, "/arm-io/dart-mtp", "reg",
            EDIT_WORDS("dart-mtp", "reg", 0x94800000, 1, 0xc000, 0, 0, 0, 0, 0));
    REFUSED("an entry that wraps around", AppleGetMtp, RETURN_INVALID_PARAMETER, "/arm-io/dockchannel-mtp", "reg",
            EDIT_WORDS("dockchannel-mtp", "reg", 0xfffff000, 0xffffffff, 0x1001, 0));
    REFUSED("a shared entry of size 0", AppleGetMtp, RETURN_INVALID_PARAMETER, "/arm-io/dart-mtp", "reg",
            EDIT_WORDS("dart-mtp", "reg", 0x94800000, 1, 0xc000, 0, 0x882a4000, 1, 0, 0));
    REFUSED("\"interrupts\" empty", AppleGetMtp, RETURN_INVALID_PARAMETER, "/arm-io/dart-mtp", "interrupts", EDIT_BYTES("dart-mtp", "interrupts", ""));
    REFUSED("\"interrupts\" of one and a half value", AppleGetMtp, RETURN_INVALID_PARAMETER, "/arm-io/mtp", "interrupts", EDIT_BYTES("mtp", "interrupts", "\1\2\3\4\5\6"));

    {
        UINT32          Lines[APPLE_DEVICE_MAX_LINES + 1] = { 0 };
        UINT64          Ranges[APPLE_DEVICE_MAX_RANGES + 1][2];
        CONST TEST_EDIT ManyLines[]    = { { "mtp", "interrupts", Lines, (APPLE_DEVICE_MAX_LINES - 2) * sizeof(UINT32) } };
        CONST TEST_EDIT TooManyLines[] = { { "mtp", "interrupts", Lines, (APPLE_DEVICE_MAX_LINES - 1) * sizeof(UINT32) } };
        CONST TEST_EDIT TooManyRanges[] = { { "mtp", "reg", Ranges, (APPLE_DEVICE_MAX_RANGES - 8) * sizeof(Ranges[0]) } };
        CONST TEST_EDIT NoRoomForSram[] = { { "mtp", "reg", Ranges, (APPLE_DEVICE_MAX_RANGES - 9) * sizeof(Ranges[0]) } };
        TEST_MACHINE    Many = mM4MacBookProWhole;
        APPLE_DEVICE    Device;
        UINTN           Index;

        for(Index = 0; Index < ARRAY_SIZE(Ranges); Index++) {
            Ranges[Index][0] = 0x100000000ULL + Index * 0x1000;
            Ranges[Index][1] = 0x1000;
        }
        Many.EditCount = 1;
        Many.Edits     = ManyLines;
        BuildMachine(&Many);
        Expect(AppleGetMtp(BuiltTree(), &Device) == RETURN_SUCCESS && Device.LineCount == APPLE_DEVICE_MAX_LINES, "as many lines as there is room for");
        CheckRefused("one line more than there is room for", TooManyLines, 1, AppleGetMtp, RETURN_UNSUPPORTED, "/arm-io/dockchannel-mtp", "interrupts");
        CheckRefused("one range more than there is room for", TooManyRanges, 1, AppleGetMtp, RETURN_UNSUPPORTED, "/arm-io/dockchannel-mtp", "reg");
        Many.Edits = NoRoomForSram;
        BuildMachine(&Many);
        Expect(AppleGetMtp(BuiltTree(), &Device) == RETURN_SUCCESS && Device.RangeCount == APPLE_DEVICE_MAX_RANGES && Device.SramStatus == RETURN_UNSUPPORTED,
               "as many ranges as there is room for, and none for the SRAM");
    }
}

STATIC VOID CheckSmc(VOID)
{
    Check("SMC of the M4 MacBook Pro", NULL, 0, AppleGetSmc, M4_SMC);
    CHECK("SMC with one range and one line", AppleGetSmc, "smc-0 38c600000+88000\n" "smc-region 38de00000+100000\n" "smc-0 AIC 562\n" SRAM_FOUND,
          EDIT_WORDS("smc", "reg", 0x8c600000, 1, 0x88000, 0), EDIT_WORDS("smc", "interrupts", 562));
    CHECK("no MTP: SMC all the same", AppleGetSmc, M4_SMC, EDIT_NO_NODE("mtp"), EDIT_NO_NODE("dart-mtp"), EDIT_NO_NODE("dockchannel-mtp"));
    CHECK("no SMC: MTP all the same", AppleGetMtp, M4_MTP, EDIT_NO_NODE("smc"));

    CHECK("no iop-smc-nub: no SRAM", AppleGetSmc, M4_SMC_RANGES M4_SMC_LINES SRAM_NOT_FOUND, EDIT_NO_NODE("iop-smc-nub"));
    CHECK("no region-base: no SRAM", AppleGetSmc, M4_SMC_RANGES M4_SMC_LINES SRAM_NOT_FOUND, EDIT_NO_PROPERTY("iop-smc-nub", "region-base"));
    CHECK("no region-size: no SRAM", AppleGetSmc, M4_SMC_RANGES M4_SMC_LINES SRAM_NOT_FOUND, EDIT_NO_PROPERTY("iop-smc-nub", "region-size"));
    CHECK("region-base of 4 bytes: no SRAM", AppleGetSmc, M4_SMC_RANGES M4_SMC_LINES SRAM_INVALID, EDIT_WORDS("iop-smc-nub", "region-base", 0x8de00000));
    CHECK("region-size of 12 bytes: no SRAM", AppleGetSmc, M4_SMC_RANGES M4_SMC_LINES SRAM_INVALID, EDIT_WORDS("iop-smc-nub", "region-size", 0x100000, 0, 0));
    CHECK("region-size 0: no SRAM", AppleGetSmc, M4_SMC_RANGES M4_SMC_LINES SRAM_INVALID, EDIT_WORDS("iop-smc-nub", "region-size", 0, 0));
    CHECK("a region that wraps around: no SRAM", AppleGetSmc, M4_SMC_RANGES M4_SMC_LINES SRAM_INVALID, EDIT_WORDS("iop-smc-nub", "region-size", 0xffffffff, 0xffffffff));

    REFUSED("no smc", AppleGetSmc, RETURN_NOT_FOUND, "/arm-io/smc", NULL, EDIT_NO_NODE("smc"));
    REFUSED("smc without \"reg\"", AppleGetSmc, RETURN_NOT_FOUND, "/arm-io/smc", "reg", EDIT_NO_PROPERTY("smc", "reg"));
    REFUSED("smc without \"interrupts\"", AppleGetSmc, RETURN_NOT_FOUND, "/arm-io/smc", "interrupts", EDIT_NO_PROPERTY("smc", "interrupts"));
    REFUSED("smc with an entry of size 0", AppleGetSmc, RETURN_INVALID_PARAMETER, "/arm-io/smc", "reg", EDIT_WORDS("smc", "reg", 0x8c600000, 1, 0, 0));
}

//
// No /arm-io that can be used: nothing can be translated
//
STATIC VOID CheckNoArmIo(VOID)
{
    APPLE_DEVICE  Device;
    RETURN_STATUS Status;

    mUsed = 0;
    Node(1, 0);
    Name("device-tree");
    Status = AppleGetSmc(BuiltTree(), &Device);
    Expect(Status == RETURN_NOT_FOUND && strcmp(Device.FailedNode, "/arm-io") == 0 && Device.FailedProperty == NULL, "no /arm-io");
}

int main(void)
{
    CheckMtp();
    CheckSmc();
    CheckNoArmIo();
    return Verdict();
}
