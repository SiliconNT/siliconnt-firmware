/** @file
  Part of AcpiTablesTest.c: the nodes of MTP and SMC
  (Drivers/AcpiPlatformDxe/CoprocessorNode.c, DeviceProperties.c). Included there
  once, after its helpers.

  MtpNode.asl and SmcNode.asl are the nodes the M4 MacBook Pro must get. A case is a
  change to its device tree (CoprocessorTree.h) and the change that makes to these
  nodes, line by line. What the firmware builds is written next to the ASL, for
  run.sh to compare.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "../../../../Silicon/Apple/AppleSiliconPkg/Test/AppleDTLibHostTest/CoprocessorTree.h"

// A line that holds Match, the only one that does: instead of it Line, or nothing
typedef struct
{
    CONST CHAR8 *Match;
    CONST CHAR8 *Line;
} ASL_EDIT;

typedef struct
{
    CONST CHAR8               *Name;
    CONST TEST_EDIT           *Edits;           // of the tree of the M4 MacBook Pro
    UINTN                     EditCount;
    CONST ASL_EDIT            *Mtp;             // of the nodes
    UINTN                     MtpCount;
    CONST ASL_EDIT            *Smc;
    UINTN                     SmcCount;
    CONST CHAR8               *MtpSkipped;      // one line for each property that is skipped; NULL: none
    CONST CHAR8               *SmcSkipped;
    COPROCESSOR_NODE_OUTCOME  MtpOutcome;
    COPROCESSOR_NODE_OUTCOME  SmcOutcome;
    CONST TEST_MACHINE        *Machine;         // NULL: the M4 MacBook Pro
} NODE_CASE;

STATIC COPROCESSOR_NODE mMtp;
STATIC COPROCESSOR_NODE mSmc;
STATIC CHAR8            mMtpAsl[32768];
STATIC CHAR8            mSmcAsl[32768];

// The lines of Asl after the edits, appended to Out. FALSE when an edit does not match exactly one line.
STATIC BOOLEAN EditAsl(IN CONST CHAR8 *Asl, IN CONST ASL_EDIT *Edits, IN UINTN Count, IN OUT CHAR8 *Out, IN OUT UINTN *Used, IN UINTN Room)
{
    CONST CHAR8 *Line = Asl;
    UINTN       Matches[64] = { 0 };
    UINTN       Index;
    BOOLEAN     Ok = TRUE;

    while(*Line != 0) {
        CONST CHAR8 *End   = strchr(Line, '\n');
        CONST CHAR8 *Put   = Line;
        UINTN       Length = End + 1 - Line;
        CHAR8       Text[8192];

        memcpy(Text, Line, Length);
        Text[Length] = 0;
        for(Index = 0; Index < Count; Index++) {
            if(strstr(Text, Edits[Index].Match) != NULL) {
                Matches[Index]++;
                Put = Edits[Index].Line;
            }
        }
        if(Put == Line) {
            *Used += snprintf(Out + *Used, Room - *Used, "%s", Text);
        } else if(Put != NULL) {
            *Used += snprintf(Out + *Used, Room - *Used, "%s\n", Put);
        }
        Line = End + 1;
    }
    for(Index = 0; Index < Count; Index++) {
        if(Matches[Index] != 1) {
            printf("      %u lines of the ASL hold %s\n", (unsigned)Matches[Index], Edits[Index].Match);
            Ok = FALSE;
        }
    }
    return Ok;
}

STATIC VOID ReadAsl(IN CONST CHAR8 *FileName, OUT CHAR8 *Asl, IN UINTN Room)
{
    Asl[ReadStatic(FileName, (UINT8 *)Asl, Room - 1)] = 0;
}

// The properties that were skipped: the key, the length in the tree, why
STATIC CONST CHAR8 *DumpSkipped(IN CONST DEVICE_PROPERTIES *Properties)
{
    STATIC CONST CHAR8 *Reasons[] = { "value", "reference", "room" };
    STATIC CHAR8       Text[16384];
    UINTN              Used = 0;
    UINTN              Index;

    Text[0] = 0;
    for(Index = 0; Index < Properties->SkippedCount && Index < DEVICE_MAX_SKIPPED; Index++) {
        Used += snprintf(Text + Used, sizeof(Text) - Used, "%s %u %s\n", Properties->Skipped[Index].Key, (unsigned)Properties->Skipped[Index].Length,
                         Reasons[Properties->Skipped[Index].Reason]);
    }
    if(Properties->SkippedCount > DEVICE_MAX_SKIPPED) {
        snprintf(Text + Used, sizeof(Text) - Used, "%u in all\n", (unsigned)Properties->SkippedCount);
    }
    return Text;
}

STATIC VOID FindNodes(IN CONST APPLE_BOOT_INFO *Info)
{
    memset(&mMtp, 0x5a, sizeof(mMtp));
    memset(&mSmc, 0x5a, sizeof(mSmc));
    FindMtpNode(Info, &mMtp);
    FindSmcNode(Info, &mSmc);
}

//
// The DSDT of Machine as AcpiPlatformDxe builds it, with the disk controller, MTP
// and SMC if there is a node for them, into mTable
//
STATIC UINTN BuildWhole(IN CONST TEST_MACHINE *Machine, OUT APPLE_CPU *Cpus, OUT UINTN *CpuCount, OUT UINT32 *ChipId)
{
    UINT64             UartBase;
    ANS_NODE           Ans;
    ANS_DEVICE         AnsDevice;
    CONST DSDT_DEVICE  *Devices[3];
    UINTN              Count = 0;
    UINTN              Size;

    if(!ReadMachine(Machine, Cpus, CpuCount, &UartBase, ChipId)) {
        return 0;
    }
    FindAnsNode(BuiltTree(), &Ans);
    FindNodes(BuiltTree());
    if(Ans.Outcome == AnsNodeMade) {
        DescribeAns(&Ans.Node, &AnsDevice);
        Devices[Count++] = &AnsDevice.Dsdt;
    }
    if(mMtp.Outcome == CoprocessorNodeMade) {
        Devices[Count++] = &mMtp.Dsdt;
    }
    if(mSmc.Outcome == CoprocessorNodeMade) {
        Devices[Count++] = &mSmc.Dsdt;
    }
    memset(mTable, 0xa5, sizeof(mTable));
    Size = DsdtSize(Cpus, *CpuCount, Devices, Count);
    if(Size >= sizeof(mTable) || DsdtBuild(*ChipId, CREATOR_REVISION, Cpus, *CpuCount, Devices, Count, mTable) != RETURN_SUCCESS || mTable[Size] != 0xa5) {
        return 0;
    }
    return Size;
}

STATIC VOID CheckNodeCase(IN CONST NODE_CASE *Case)
{
    STATIC CHAR8 Asl[262144];
    APPLE_CPU    Cpus[APPLE_MAX_CPUS];
    UINTN        CpuCount;
    UINT32       ChipId = 0;
    TEST_MACHINE Machine = Case->Machine != NULL ? *Case->Machine : mM4MacBookProWhole;
    UINTN        Size;
    UINTN        Used = 0;
    UINTN        Index;
    CHAR8        Line[256];
    BOOLEAN      Ok;

    Machine.Edits     = Case->Edits;
    Machine.EditCount = Case->EditCount;
    Size              = BuildWhole(&Machine, Cpus, &CpuCount, &ChipId);

    snprintf(Line, sizeof(Line), "%s: MTP outcome %d, SMC outcome %d, %u bytes of DSDT, nothing written beyond it", Case->Name, (int)Case->MtpOutcome, (int)Case->SmcOutcome, (unsigned)Size);
    if(!Expect(Size != 0 && mMtp.Outcome == Case->MtpOutcome && mSmc.Outcome == Case->SmcOutcome, Line)) {
        printf("      MTP outcome %d, SMC outcome %d\n", (int)mMtp.Outcome, (int)mSmc.Outcome);
        return;
    }

    snprintf(Line, sizeof(Line), "%s: what is skipped", Case->Name);
    Ok = strcmp(DumpSkipped(&mMtp.Properties), Case->MtpSkipped != NULL ? Case->MtpSkipped : "") == 0;
    if(!Ok) {
        printf("MTP:\n%s", DumpSkipped(&mMtp.Properties));
    }
    if(strcmp(DumpSkipped(&mSmc.Properties), Case->SmcSkipped != NULL ? Case->SmcSkipped : "") != 0) {
        printf("SMC:\n%s", DumpSkipped(&mSmc.Properties));
        Ok = FALSE;
    }
    Expect(Ok, Line);

    WriteOutput(Case->Name, "aml", mTable, Size);
    Used = snprintf(Asl, sizeof(Asl), "DefinitionBlock (\"\", \"DSDT\", 2, \"Apple \", \"APPLEEFI\", 0x%X) {\n  Scope (\\_SB) {\n", (unsigned)ChipId);
    for(Index = 0; Index < CpuCount; Index++) {
        Used += snprintf(Asl + Used, sizeof(Asl) - Used, "    Device (CPU%u) { Name (_HID, \"ACPI0007\") Name (_UID, %u) }\n", (unsigned)Cpus[Index].Number, (unsigned)Cpus[Index].Number);
    }
    if(Machine.Ans != NULL && Machine.AicToGic != NULL) {
        Used += WriteAnsAsl(Asl + Used, sizeof(Asl) - Used, &mM4Node);
    }
    if(Case->MtpOutcome == CoprocessorNodeMade) {
        Ok = EditAsl(mMtpAsl, Case->Mtp, Case->MtpCount, Asl, &Used, sizeof(Asl)) && Ok;
    }
    if(Case->SmcOutcome == CoprocessorNodeMade) {
        Ok = EditAsl(mSmcAsl, Case->Smc, Case->SmcCount, Asl, &Used, sizeof(Asl)) && Ok;
    }
    Used += snprintf(Asl + Used, sizeof(Asl) - Used, "  }\n}\n");
    WriteOutput(Case->Name, "asl", Asl, Used);
    snprintf(Line, sizeof(Line), "%s: the changes of the ASL", Case->Name);
    if(!Ok) {
        Expect(FALSE, Line);
    }
}

#define EDITS(...)  (CONST TEST_EDIT[]){ __VA_ARGS__ }, sizeof((CONST TEST_EDIT[]){ __VA_ARGS__ }) / sizeof(TEST_EDIT)
#define ASL(...)    (CONST ASL_EDIT[]){ __VA_ARGS__ }, sizeof((CONST ASL_EDIT[]){ __VA_ARGS__ }) / sizeof(ASL_EDIT)
#define NO_ASL      NULL, 0

// A property of the nodes as a line of ASL; the line of the M4 MacBook Pro that has this key
#define PROPERTY(Key, Value)  "                Package (2) { \"" Key "\", " Value " },"
#define KEY(Key)              "{ \"" Key "\","

// The memory-names of MTP with other names in the place of the DART's
#define MEMORY_NAMES(Mtp, Dart, Dock, Sram) \
    { KEY("memory-names"), PROPERTY("memory-names", "Package () { " Mtp ", " Dart ", " Dock Sram " }") }
#define MTP_NAMES   "\"mtp-0\", \"mtp-1\""
#define DART_NAMES  "\"dart-mtp-0\", \"dart-mtp-1\""
#define DOCK_NAMES  "\"dockchannel-mtp-0\", \"dockchannel-mtp-1\", \"dockchannel-mtp-2\", \"dockchannel-mtp-3\", \"dockchannel-mtp-4\", \"dockchannel-mtp-5\""
#define SRAM_NAME   ", \"mtp-sram\""

#define QWORD(Base, Last, Size) \
    "            QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0x0, " Base ", " Last ", 0x0, " Size ")"
#define GSIV(Number)  "            Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { " Number " }"

#define CASE(...) \
    do { \
        CONST NODE_CASE Case = { __VA_ARGS__ }; \
        CheckNodeCase(&Case); \
    } while(0)

// Every property of the node by itself left out of the tree: its line is not there, nothing else changes
STATIC VOID CheckEveryPropertyLeftOut(IN CONST CHAR8 *Asl, IN BOOLEAN Smc)
{
    CONST CHAR8 *At    = Asl;
    UINTN       Number = 0;

    while((At = strstr(At, "Package (2) { \"")) != NULL) {
        CHAR8       Key[128];
        CHAR8       Node[64];
        CHAR8       Match[160];
        CHAR8       Name[64];
        CHAR8       *Slash;
        CONST CHAR8 *Property;

        At += sizeof("Package (2) { \"") - 1;
        memcpy(Key, At, strchr(At, '"') - At);
        Key[strchr(At, '"') - At] = 0;
        Slash = strchr(Key, '/');
        if(Slash == NULL || strcmp(Key, "mtp-transport/interfaces") == 0) {
            continue;
        }
        memcpy(Node, Key, Slash - Key);
        Node[Slash - Key] = 0;
        Property = strcmp(Slash + 1, "iommu-stream") == 0 ? "iommu-parent" : Slash + 1;
        snprintf(Match, sizeof(Match), "{ \"%s\",", Key);
        snprintf(Name, sizeof(Name), "%s-without-property-%02u", Smc ? "smc" : "mtp", (unsigned)Number++);
        {
            CONST TEST_EDIT Edits[]  = { { Node, Property, NULL, 0 } };
            CONST ASL_EDIT  Change[] = { { Match, NULL } };
            CONST NODE_CASE Case = { Name, Edits, 1, Smc ? NULL : Change, Smc ? 0 : 1, Smc ? Change : NULL, Smc ? 1 : 0 };

            printf("ok    %s is without %s\n", Name, Key);
            CheckNodeCase(&Case);
        }
    }
    Expect(Number == (Smc ? 44 : 45), Smc ? "SMC: 44 properties were left out, each by itself" : "MTP: 45 properties were left out, each by itself");
}

// 32-bit words as a package in ASL: Word (Index) for Index from First
STATIC UINT32 MadeUpWord(IN UINTN Index)
{
    return (UINT32)(Index * 0x01234567 + Index / 3);
}

STATIC CONST CHAR8 *WordsLine(OUT CHAR8 *Line, IN UINTN Room, IN CONST CHAR8 *Key, IN UINTN First, IN UINTN Count)
{
    UINTN Used;
    UINTN Index;

    Used = snprintf(Line, Room, "                Package (2) { \"%s\", Package () {", Key);
    for(Index = First; Index < First + Count; Index++) {
        Used += snprintf(Line + Used, Room - Used, "%s0x%X", Index != First ? ", " : " ", (unsigned)MadeUpWord(Index));
    }
    snprintf(Line + Used, Room - Used, " } },");
    return Line;
}

// The DAPF of this many words, written as these parts
STATIC VOID CheckDapf(IN CONST CHAR8 *Name, IN UINTN Words, IN UINTN Parts)
{
    STATIC CHAR8  Lines[65536];
    UINT32        Value[1024];
    UINTN         Used = 0;
    UINTN         Part;
    UINTN         Index;

    for(Index = 0; Index < Words; Index++) {
        Value[Index] = MadeUpWord(Index);
    }
    for(Part = 0; Part < Parts; Part++) {
        CHAR8 Key[64];

        snprintf(Key, sizeof(Key), Part == 0 ? "dart-mtp/dapf-instance-0" : "dart-mtp/dapf-instance-0+%u", (unsigned)Part);
        WordsLine(Lines + Used, sizeof(Lines) - Used, Key, Part * 255, Part + 1 < Parts ? 255 : Words - Part * 255);
        Used += strlen(Lines + Used);
        if(Part + 1 < Parts) {
            Lines[Used++] = '\n';
        }
    }
    {
        CONST TEST_EDIT Edits[]  = { { "dart-mtp", "dapf-instance-0", Value, (UINT32)(Words * 4) } };
        CONST ASL_EDIT  Change[] = { { KEY("dart-mtp/dapf-instance-0"), Lines } };

        CASE(Name, Edits, 1, Change, 1);
    }
}

//
// More children than the tree has: Count of them below the nodes of the M4 MacBook
// Pro's tree, after the node called After
//
STATIC TEST_MACHINE WithChildren(IN CONST CHAR8 *After, IN CONST TEST_NODE *Children, IN UINTN Count)
{
    STATIC TEST_NODE Nodes[1024];
    TEST_MACHINE     Machine = mM4MacBookProWhole;
    UINTN            Used    = 0;
    UINTN            Index;
    UINTN            Child;

    for(Index = 0; Index < ARRAY_SIZE(mCoprocessorsOfM4MacBookPro); Index++) {
        Nodes[Used++] = mCoprocessorsOfM4MacBookPro[Index];
        if(strcmp(mCoprocessorsOfM4MacBookPro[Index].Name, After) == 0) {
            for(Child = 0; Child < Count; Child++) {
                Nodes[Used++] = Children[Child];
            }
        }
    }
    Machine.ArmIoNodes     = Nodes;
    Machine.ArmIoNodeCount = Used;
    return Machine;
}

STATIC VOID CheckResources(VOID)
{
    TEST_MACHINE Machine;
    TEST_PMGR    Pmgr;

    CASE("whole", NULL, 0, NO_ASL, NO_ASL);

    CASE("dart-two-ranges", EDITS(EDIT_WORDS("dart-mtp", "reg", 0x94800000, 1, 0xc000, 0, 0x94810000, 1, 0x4000, 0)));
    CASE("dart-one-range", EDITS(EDIT_WORDS("dart-mtp", "reg", 0x94800000, 1, 0xc000, 0)),
         ASL({ "memory dart-mtp-1", NULL }, MEMORY_NAMES(MTP_NAMES, "\"dart-mtp-0\"", DOCK_NAMES, SRAM_NAME)));
    CASE("twelve-dockchannel-ranges",
         EDITS(EDIT_WORDS("dockchannel-mtp", "reg",
                          0x94b00000, 1, 0x1000, 0, 0x94b14000, 1, 0x1000, 0, 0x94b30000, 1, 0x1000, 0, 0x94b34000, 1, 0x1000, 0, 0x94b28000, 1, 0x1000, 0,
                          0x94b2c000, 1, 0x1000, 0, 0xa0000000, 1, 1, 0, 0xa0000001, 1, 1, 0, 0xa0000002, 1, 1, 0, 0xa0000003, 1, 1, 0, 0xa0000004, 1, 1, 0,
                          0xffffffff, 0xffffffff, 1, 0)),
         ASL({ "memory dockchannel-mtp-5",
               QWORD("0x394B2C000", "0x394B2CFFF", "0x1000") "\n" QWORD("0x3A0000000", "0x3A0000000", "0x1") "\n" QWORD("0x3A0000001", "0x3A0000001", "0x1") "\n"
               QWORD("0x3A0000002", "0x3A0000002", "0x1") "\n" QWORD("0x3A0000003", "0x3A0000003", "0x1") "\n" QWORD("0x3A0000004", "0x3A0000004", "0x1") "\n"
               QWORD("0xFFFFFFFFFFFFFFFF", "0xFFFFFFFFFFFFFFFF", "0x1") },
             MEMORY_NAMES(MTP_NAMES, DART_NAMES, DOCK_NAMES ", \"dockchannel-mtp-6\", \"dockchannel-mtp-7\", \"dockchannel-mtp-8\", \"dockchannel-mtp-9\", "
                          "\"dockchannel-mtp-10\", \"dockchannel-mtp-11\"", SRAM_NAME)));

    // The power manager elsewhere: PS_PROT is listed
    {
        STATIC CONST UINT64 Reg[][2] = { { 0x180700000ULL, 0xcc000 }, { 0x188300000ULL, 0x80000 } };

        Pmgr           = mPmgrOfM4MacBookPro;
        Pmgr.Reg       = Reg;
        Pmgr.RegLength = sizeof(Reg);
        Machine        = mM4MacBookProWhole;
        Machine.Pmgr   = &Pmgr;
        CASE("power-manager-elsewhere", NULL, 0,
             ASL({ "memory dart-mtp-1", QWORD("0x394810000", "0x394813FFF", "0x4000") "\n" QWORD("0x3882A4000", "0x3882A7FFF", "0x4000") },
                 MEMORY_NAMES(MTP_NAMES, DART_NAMES ", \"dart-mtp-2\"", DOCK_NAMES, SRAM_NAME)),
             .Machine = &Machine);
        Expect(!mMtp.Device.Ranges[4].PowerManager, "power-manager-elsewhere: no range is said to be shared");
    }

    CASE("no-carveout-memory-map", EDITS(EDIT_NO_NODE("carveout-memory-map")),
         ASL({ "memory mtp-sram", NULL }, MEMORY_NAMES(MTP_NAMES, DART_NAMES, DOCK_NAMES, "")));
    CASE("no-region-for-the-sram", EDITS(EDIT_NO_PROPERTY("carveout-memory-map", "region-id-91")),
         ASL({ "memory mtp-sram", NULL }, MEMORY_NAMES(MTP_NAMES, DART_NAMES, DOCK_NAMES, "")));
    CASE("no-region-base", EDITS(EDIT_NO_PROPERTY("iop-smc-nub", "region-base")), NO_ASL,
         ASL({ "memory smc-region", NULL }, { KEY("memory-names"), PROPERTY("memory-names", "Package () { \"smc-0\", \"smc-1\", \"smc-2\" }") }));
    CASE("no-region-size", EDITS(EDIT_NO_PROPERTY("iop-smc-nub", "region-size")), NO_ASL,
         ASL({ "memory smc-region", NULL }, { KEY("memory-names"), PROPERTY("memory-names", "Package () { \"smc-0\", \"smc-1\", \"smc-2\" }") }));

    // Other lines: two for the DART, one for SMC
    CASE("other-lines", EDITS(EDIT_WORDS("dart-mtp", "interrupts", 1505, 0), EDIT_WORDS("smc", "interrupts", 561)),
         ASL({ "line dart-mtp-0", GSIV("428") "\n" GSIV("32") },
             { KEY("interrupt-names"), PROPERTY("interrupt-names", "Package () { \"mtp-0\", \"mtp-1\", \"mtp-2\", \"mtp-3\", \"dart-mtp-0\", \"dart-mtp-1\", \"dockchannel-mtp-0\" }") }),
         ASL({ "line smc-0", NULL }, { "line smc-2", NULL }, { "line smc-3", NULL }, { KEY("interrupt-names"), PROPERTY("interrupt-names", "Package () { \"smc-0\" }") }));

    //
    // All that is there belongs to the power manager: no range, an empty list of names
    //
    {
        STATIC CONST UINT64 Reg[][2] = { { 0x100000000ULL, 0x100000000ULL } };

        Pmgr.Reg       = Reg;
        Pmgr.RegLength = sizeof(Reg);
        Machine.Ans    = NULL;
        CASE("all-of-it-in-the-power-manager", NULL, 0,
             ASL({ "memory mtp-0", NULL }, { "memory mtp-1", NULL }, { "memory dart-mtp-0", NULL }, { "memory dart-mtp-1", NULL },
                 { "memory dockchannel-mtp-0", NULL }, { "memory dockchannel-mtp-1", NULL }, { "memory dockchannel-mtp-2", NULL }, { "memory dockchannel-mtp-3", NULL },
                 { "memory dockchannel-mtp-4", NULL }, { "memory dockchannel-mtp-5", NULL }, { "memory mtp-sram", NULL },
                 { KEY("memory-names"), PROPERTY("memory-names", "Package () {}") }),
             ASL({ "memory smc-0", NULL }, { "memory smc-1", NULL }, { "memory smc-2", NULL }, { "memory smc-region", NULL },
                 { KEY("memory-names"), PROPERTY("memory-names", "Package () {}") }),
             .Machine = &Machine);
    }
}

//
// No node: which step failed. The other node and the rest of the DSDT are as they were.
//
STATIC VOID CheckNoNode(VOID)
{
    STATIC UINT8  OnlyAns[4096];
    UINTN         OnlyAnsSize;
    APPLE_CPU     Cpus[APPLE_MAX_CPUS];
    UINTN         CpuCount;
    UINT32        ChipId;
    TEST_MACHINE  Machine;

    CASE("no-mtp", EDITS(EDIT_NO_NODE("mtp")), .MtpOutcome = CoprocessorNodeNoDevice);
    Expect(mMtp.Status == RETURN_NOT_FOUND && strcmp(mMtp.Device.FailedNode, "/arm-io/mtp") == 0 && mMtp.Device.FailedProperty == NULL, "no-mtp: that is what it says");
    CASE("no-dart-mtp", EDITS(EDIT_NO_NODE("dart-mtp")), .MtpOutcome = CoprocessorNodeNoDevice);
    CASE("no-dockchannel-mtp", EDITS(EDIT_NO_NODE("dockchannel-mtp")), .MtpOutcome = CoprocessorNodeNoDevice);
    CASE("no-reg-mtp", EDITS(EDIT_NO_PROPERTY("mtp", "reg")), .MtpOutcome = CoprocessorNodeNoDevice);
    CASE("no-reg-dart-mtp", EDITS(EDIT_NO_PROPERTY("dart-mtp", "reg")), .MtpOutcome = CoprocessorNodeNoDevice);
    CASE("no-reg-dockchannel-mtp", EDITS(EDIT_NO_PROPERTY("dockchannel-mtp", "reg")), .MtpOutcome = CoprocessorNodeNoDevice);
    CASE("no-interrupts-mtp", EDITS(EDIT_NO_PROPERTY("mtp", "interrupts")), .MtpOutcome = CoprocessorNodeNoDevice);
    CASE("no-interrupts-dart-mtp", EDITS(EDIT_NO_PROPERTY("dart-mtp", "interrupts")), .MtpOutcome = CoprocessorNodeNoDevice);
    CASE("no-interrupts-dockchannel-mtp", EDITS(EDIT_NO_PROPERTY("dockchannel-mtp", "interrupts")), .MtpOutcome = CoprocessorNodeNoDevice);
    Expect(mMtp.Status == RETURN_NOT_FOUND && strcmp(mMtp.Device.FailedNode, "/arm-io/dockchannel-mtp") == 0 && strcmp(mMtp.Device.FailedProperty, "interrupts") == 0,
           "no-interrupts-dockchannel-mtp: that is what it says");

    //
    // Without smc the reset lines of the trackpad and of stm point nowhere; MTP is described without them
    //
    CASE("no-smc", EDITS(EDIT_NO_NODE("smc")), ASL({ KEY("multi-touch/function-afe-reset"), NULL }, { KEY("stm/function-stm-reset"), NULL }), NO_ASL,
         "multi-touch/function-afe-reset 16 reference\n" "stm/function-stm-reset 16 reference\n", NULL, CoprocessorNodeMade, CoprocessorNodeNoDevice);
    Expect(mSmc.Status == RETURN_NOT_FOUND && strcmp(mSmc.Device.FailedNode, "/arm-io/smc") == 0, "no-smc: that is what it says");
    CASE("no-reg-smc", EDITS(EDIT_NO_PROPERTY("smc", "reg")), .SmcOutcome = CoprocessorNodeNoDevice);
    CASE("no-interrupts-smc", EDITS(EDIT_NO_PROPERTY("smc", "interrupts")), .SmcOutcome = CoprocessorNodeNoDevice);

    //
    // A line that is not in the table of interrupt numbers: 2000 for each in turn
    //
    CASE("no-line-mtp-0", EDITS(EDIT_WORDS("mtp", "interrupts", 2000, 1113, 1116, 1115)), .MtpOutcome = CoprocessorNodeLineMissing);
    Expect(mMtp.MissingLine == 0 && mMtp.Device.Lines[0].Aic == 2000, "no-line-mtp-0: that is what it says");
    CASE("no-line-mtp-1", EDITS(EDIT_WORDS("mtp", "interrupts", 1114, 2000, 1116, 1115)), .MtpOutcome = CoprocessorNodeLineMissing);
    CASE("no-line-mtp-2", EDITS(EDIT_WORDS("mtp", "interrupts", 1114, 1113, 2000, 1115)), .MtpOutcome = CoprocessorNodeLineMissing);
    CASE("no-line-mtp-3", EDITS(EDIT_WORDS("mtp", "interrupts", 1114, 1113, 1116, 2000)), .MtpOutcome = CoprocessorNodeLineMissing);
    CASE("no-line-dart-mtp", EDITS(EDIT_WORDS("dart-mtp", "interrupts", 2000)), .MtpOutcome = CoprocessorNodeLineMissing);
    CASE("no-line-dockchannel-mtp", EDITS(EDIT_WORDS("dockchannel-mtp", "interrupts", 2000)), .MtpOutcome = CoprocessorNodeLineMissing);
    Expect(mMtp.MissingLine == 5, "no-line-dockchannel-mtp: that is what it says");
    CASE("no-line-smc-0", EDITS(EDIT_WORDS("smc", "interrupts", 2000, 561, 564, 563)), .SmcOutcome = CoprocessorNodeLineMissing);
    CASE("no-line-smc-1", EDITS(EDIT_WORDS("smc", "interrupts", 562, 2000, 564, 563)), .SmcOutcome = CoprocessorNodeLineMissing);
    CASE("no-line-smc-2", EDITS(EDIT_WORDS("smc", "interrupts", 562, 561, 2000, 563)), .SmcOutcome = CoprocessorNodeLineMissing);
    CASE("no-line-smc-3", EDITS(EDIT_WORDS("smc", "interrupts", 562, 561, 564, 2000)), .SmcOutcome = CoprocessorNodeLineMissing);
    Expect(mSmc.MissingLine == 3, "no-line-smc-3: that is what it says");

    // AIC 1093, of the DockChannel, taken out of the table: it is the last of them, so no other number changes
    {
        STATIC UINT32 Lines[ARRAY_SIZE(mAicToGicOfM4MacBookPro)];
        UINT32        Count = 0;

        while(Count < ARRAY_SIZE(mAicToGicOfM4MacBookPro) && mAicToGicOfM4MacBookPro[Count] != 1093) {
            Lines[Count] = mAicToGicOfM4MacBookPro[Count];
            Count++;
        }
        Machine               = mM4MacBookProWhole;
        Machine.AicToGic      = Lines;
        Machine.AicToGicCount = Count;
        CASE("table-up-to-1091", NULL, 0, .MtpOutcome = CoprocessorNodeLineMissing, .Machine = &Machine);
        Expect(mMtp.MissingLine == 0, "table-up-to-1091: the first line that is missing is the first of mtp");
    }

    Machine          = mM4MacBookProWhole;
    Machine.AicToGic = NULL;
    CASE("no-table", NULL, 0, .MtpOutcome = CoprocessorNodeNoTable, .SmcOutcome = CoprocessorNodeNoTable, .Machine = &Machine);
    Expect(mMtp.Status == RETURN_NOT_FOUND && mSmc.Status == RETURN_NOT_FOUND, "no-table: that is what it says");
    CASE("no-mtp-no-smc-no-table", EDITS(EDIT_NO_NODE("mtp"), EDIT_NO_NODE("smc")), .MtpOutcome = CoprocessorNodeNoDevice, .SmcOutcome = CoprocessorNodeNoDevice, .Machine = &Machine);

    //
    // Neither node: the DSDT of the firmware before there were these nodes, byte for byte
    //
    OnlyAnsSize = ReadStatic(STATIC_979AB44 "Dsdt.aml", OnlyAns, sizeof(OnlyAns));
    CASE("neither", EDITS(EDIT_NO_NODE("smc"), EDIT_NO_NODE("dockchannel-mtp")), .MtpOutcome = CoprocessorNodeNoDevice, .SmcOutcome = CoprocessorNodeNoDevice);
    Machine = mM4MacBookProWithAns;
    Expect(BuildWhole(&Machine, Cpus, &CpuCount, &ChipId) == OnlyAnsSize && memcmp(mTable, OnlyAns, OnlyAnsSize) == 0, "a tree with neither: the DSDT of 979ab44, byte for byte");
    Machine           = mM4MacBookProWhole;
    Machine.Edits     = (CONST TEST_EDIT[]){ EDIT_WORDS("smc", "interrupts", 2000), EDIT_NO_PROPERTY("mtp", "reg") };
    Machine.EditCount = 2;
    Expect(BuildWhole(&Machine, Cpus, &CpuCount, &ChipId) == OnlyAnsSize && memcmp(mTable, OnlyAns, OnlyAnsSize) == 0, "a tree from which neither can be described: the DSDT of 979ab44, byte for byte");

    //
    // With them, what was there before is still the same: up to the end of ANS0, but for the length of the scope
    //
    {
        UINTN Size = BuildWhole(&mM4MacBookProWhole, Cpus, &CpuCount, &ChipId);

        Expect(Size > 4095 && mTable[36] == 0x10 && mTable[37] >> 6 == 2 && memcmp(mTable + 40, OnlyAns + 39, OnlyAnsSize - 39) == 0,
               "the cores and ANS0 are as in the DSDT of 979ab44, byte for byte; the scope has three bytes of length");
    }
}

//
// Nodes that hold nothing but properties
//
STATIC VOID CheckNodesLeftOut(VOID)
{
    CASE("no-iop-mtp-nub", EDITS(EDIT_NO_NODE("iop-mtp-nub")),
         ASL({ KEY("iop-mtp-nub/compatible"), NULL }, { KEY("iop-mtp-nub/pre-loaded"), NULL }, { KEY("iop-mtp-nub/no-shutdown"), NULL },
             { KEY("iop-mtp-nub/coredump-enable"), NULL }, { KEY("iop-mtp-nub/watchdog-enable"), NULL }, { KEY("iop-mtp-nub/no-firmware-service"), NULL },
             { KEY("iop-mtp-nub/sleep-on-hibernate"), NULL }, { KEY("iop-mtp-nub/cold-boot-after-hibernate"), NULL }));
    CASE("no-mapper-mtp", EDITS(EDIT_NO_NODE("mapper-mtp")), ASL({ KEY("mtp/iommu-stream"), NULL }, { KEY("mtp-transport/iommu-stream"), NULL }), NO_ASL,
         "mtp/iommu-parent 4 reference\n" "mtp-transport/iommu-parent 4 reference\n");
    CASE("no-mtp-transport", EDITS(EDIT_NO_NODE("mtp-transport")),
         ASL({ "{ \"mtp-transport/interfaces\"", NULL }, { "{ \"mtp-transport/iommu-stream\"", NULL }, { "{ \"mtp-transport/hid-transport-mux\"", NULL },
             { "{ \"mtp-transport/hid-merge-personality\"", NULL }, { "{ \"mtp-transport/image-tag\"", NULL }, { "{ \"multi-touch/bootloader-type\"", NULL },
             { "{ \"multi-touch/image-tag\"", NULL }, { "{ \"multi-touch/hid-merge-personality\"", NULL }, { "{ \"multi-touch/function-afe-reset\"", NULL },
             { "{ \"keyboard/kblang-calibration\"", NULL }, { "{ \"stm/bootloader-type\"", NULL }, { "{ \"stm/image-tag\"", NULL },
             { "{ \"stm/hid-merge-personality\"", NULL }, { "{ \"stm/function-stm-reset\"", NULL }));
    CASE("no-multi-touch", EDITS(EDIT_NO_NODE("multi-touch")),
         ASL({ KEY("mtp-transport/interfaces"), PROPERTY("mtp-transport/interfaces", "Package () { \"keyboard\", \"stm\", \"actuator\", \"tp-accel\" }") },
             { KEY("multi-touch/bootloader-type"), NULL }, { KEY("multi-touch/image-tag"), NULL }, { KEY("multi-touch/hid-merge-personality"), NULL },
             { KEY("multi-touch/function-afe-reset"), NULL }));
    CASE("no-interfaces", EDITS(EDIT_NO_NODE("multi-touch"), EDIT_NO_NODE("keyboard"), EDIT_NO_NODE("stm"), EDIT_NO_NODE("actuator"), EDIT_NO_NODE("tp-accel")),
         ASL({ KEY("mtp-transport/interfaces"), NULL },
             { KEY("multi-touch/bootloader-type"), NULL }, { KEY("multi-touch/image-tag"), NULL }, { KEY("multi-touch/hid-merge-personality"), NULL },
             { KEY("multi-touch/function-afe-reset"), NULL }, { KEY("keyboard/kblang-calibration"), NULL }, { KEY("stm/bootloader-type"), NULL },
             { KEY("stm/image-tag"), NULL }, { KEY("stm/hid-merge-personality"), NULL }, { KEY("stm/function-stm-reset"), NULL }));
    CASE("no-smc-pmu", EDITS(EDIT_NO_NODE("smc-pmu")), ASL({ KEY("multi-touch/function-afe-reset"), NULL }, { KEY("stm/function-stm-reset"), NULL }),
         ASL({ KEY("smc-pmu/has_lid_open_sensor"), NULL }, { KEY("smc-pmu/event_name-bit0"), NULL }, { KEY("smc-pmu/event_name-bit8"), NULL },
             { KEY("smc-pmu/event_name-bit9"), NULL }, { KEY("smc-pmu/event_name-bit10"), NULL }, { KEY("smc-pmu/event_name-bit11"), NULL },
             { KEY("smc-pmu/event_name-bit12"), NULL }, { KEY("smc-pmu/event_name-bit13"), NULL }, { KEY("smc-pmu/event_name-bit40"), NULL },
             { KEY("smc-pmu/event_name-bit41"), NULL }, { KEY("smc-pmu/event_name-bit42"), NULL }, { KEY("smc-pmu/event_name-bit43"), NULL },
             { KEY("smc-pmu/event_name-bit44"), NULL }, { KEY("smc-pmu/event_name-bit45"), NULL }, { KEY("smc-pmu/event_name-bit48"), NULL }),
         "multi-touch/function-afe-reset 16 reference\n" "stm/function-stm-reset 16 reference\n");
    CASE("no-charger-utils", EDITS(EDIT_NO_NODE("smc-charger-util-0"), EDIT_NO_NODE("smc-charger-util-1"), EDIT_NO_NODE("smc-charger-util-3"), EDIT_NO_NODE("smc-charger-util")), NO_ASL,
         ASL({ KEY("smc-charger-util-0/dock-num"), NULL }, { KEY("smc-charger-util-0/port-number"), NULL }, { KEY("smc-charger-util-0/port-type"), NULL },
             { KEY("smc-charger-util-1/dock-num"), NULL }, { KEY("smc-charger-util-1/port-number"), NULL }, { KEY("smc-charger-util-1/port-type"), NULL },
             { KEY("smc-charger-util-3/dock-num"), NULL }, { KEY("smc-charger-util-3/port-number"), NULL }, { KEY("smc-charger-util-3/port-type"), NULL },
             { KEY("smc-charger-util/dock-num"), NULL }, { KEY("smc-charger-util/port-number"), NULL }, { KEY("smc-charger-util/port-type"), NULL },
             { KEY("smc-charger-util/magsafe-auth"), NULL }));
}

//
// Values: what is there and does not fit is skipped, and said; what fits is copied
//
STATIC VOID CheckValues(VOID)
{
    // Integers
    CASE("integer-of-8-bytes", EDITS(EDIT_WORDS("dart-mtp", "page-size", 0x4000, 0)), ASL({ KEY("dart-mtp/page-size"), NULL }), NO_ASL, "dart-mtp/page-size 8 value\n");
    CASE("integer-of-3-bytes", EDITS(EDIT_BYTES("smc", "aot-power", "\1\0\0")), NO_ASL, ASL({ KEY("smc/aot-power"), NULL }), NULL, "smc/aot-power 3 value\n");
    CASE("integer-empty", EDITS(EDIT_BYTES("iop-mtp-nub", "pre-loaded", "")), ASL({ KEY("iop-mtp-nub/pre-loaded"), NULL }), NO_ASL, "iop-mtp-nub/pre-loaded 0 value\n");
    CASE("integer-values", EDITS(EDIT_WORDS("dart-mtp", "page-size", 0xffffffff), EDIT_WORDS("dart-mtp", "sid-count", 0x10000), EDIT_WORDS("dart-mtp", "dart-id", 0)),
         ASL({ KEY("dart-mtp/page-size"), PROPERTY("dart-mtp/page-size", "0xFFFFFFFF") }, { KEY("dart-mtp/sid-count"), PROPERTY("dart-mtp/sid-count", "0x10000") },
             { KEY("dart-mtp/dart-id"), PROPERTY("dart-mtp/dart-id", "0") }));

    // Present
    CASE("present-with-a-value", EDITS(EDIT_WORDS("dart-mtp", "no-sleep", 1)), ASL({ KEY("dart-mtp/no-sleep"), NULL }), NO_ASL, "dart-mtp/no-sleep 4 value\n");

    // Strings
    CASE("string-not-ended", EDITS(EDIT_BYTES("dockchannel-mtp", "channel-name", "channel0")), ASL({ KEY("dockchannel-mtp/channel-name"), NULL }), NO_ASL,
         "dockchannel-mtp/channel-name 8 value\n");
    CASE("string-empty-property", EDITS(EDIT_BYTES("dockchannel-mtp", "channel-name", "")), ASL({ KEY("dockchannel-mtp/channel-name"), NULL }), NO_ASL,
         "dockchannel-mtp/channel-name 0 value\n");
    CASE("string-empty", EDITS(EDIT_BYTES("dockchannel-mtp", "channel-name", "\0")), ASL({ KEY("dockchannel-mtp/channel-name"), PROPERTY("dockchannel-mtp/channel-name", "\"\"") }));
    CASE("string-padded", EDITS(EDIT_BYTES("dockchannel-mtp", "channel-name", "ch1\0\0\0\0\0")),
         ASL({ KEY("dockchannel-mtp/channel-name"), PROPERTY("dockchannel-mtp/channel-name", "\"ch1\"") }));
    CASE("string-two", EDITS(EDIT_BYTES("dockchannel-mtp", "channel-name", "ch1\0ch2\0")), ASL({ KEY("dockchannel-mtp/channel-name"), NULL }), NO_ASL,
         "dockchannel-mtp/channel-name 8 value\n");
    CASE("string-not-ascii", EDITS(EDIT_BYTES("smc-pmu", "event_name-bit0", "pwr\x80\0")), NO_ASL, ASL({ KEY("smc-pmu/event_name-bit0"), NULL }), NULL, "smc-pmu/event_name-bit0 5 value\n");
    CASE("string-of-control-characters", EDITS(EDIT_BYTES("smc-pmu", "event_name-bit0", "\x01\x7f\0")), NO_ASL,
         ASL({ KEY("smc-pmu/event_name-bit0"), PROPERTY("smc-pmu/event_name-bit0", "\"\\x01\\x7F\"") }));

    // Several strings
    CASE("strings-two", EDITS(EDIT_BYTES("mtp", "compatible", "iop,ascwrap-v7\0iop,ascwrap-v6\0")),
         ASL({ KEY("mtp/compatible"), PROPERTY("mtp/compatible", "Package () { \"iop,ascwrap-v7\", \"iop,ascwrap-v6\" }") }));
    CASE("strings-padded", EDITS(EDIT_BYTES("mtp", "compatible", "a\0bc\0\0\0")), ASL({ KEY("mtp/compatible"), PROPERTY("mtp/compatible", "Package () { \"a\", \"bc\" }") }));
    CASE("strings-not-ended", EDITS(EDIT_BYTES("mtp", "compatible", "a\0bc")), ASL({ KEY("mtp/compatible"), NULL }), NO_ASL, "mtp/compatible 4 value\n");
    CASE("strings-empty-between", EDITS(EDIT_BYTES("mtp", "compatible", "a\0\0bc\0")), ASL({ KEY("mtp/compatible"), NULL }), NO_ASL, "mtp/compatible 6 value\n");
    CASE("strings-none", EDITS(EDIT_BYTES("mtp", "compatible", "")), ASL({ KEY("mtp/compatible"), NULL }), NO_ASL, "mtp/compatible 0 value\n");
    CASE("strings-zeros-only", EDITS(EDIT_BYTES("mtp", "compatible", "\0\0\0\0")), ASL({ KEY("mtp/compatible"), NULL }), NO_ASL, "mtp/compatible 4 value\n");
    CASE("strings-not-ascii", EDITS(EDIT_BYTES("mtp", "compatible", "a\0\xff\0")), ASL({ KEY("mtp/compatible"), NULL }), NO_ASL, "mtp/compatible 4 value\n");
    {
        CHAR8           Many[2 * 256];
        CONST TEST_EDIT TooMany[] = { { "mtp", "compatible", Many, sizeof(Many) } };
        CONST TEST_EDIT AsManyAsFit[] = { { "mtp", "compatible", Many, sizeof(Many) - 2 } };
        CHAR8           Line[4096];
        UINTN           Used = snprintf(Line, sizeof(Line), "                Package (2) { \"mtp/compatible\", Package () { \"x\"");
        UINTN           Index;

        for(Index = 0; Index < 256; Index++) {
            Many[2 * Index]     = 'x';
            Many[2 * Index + 1] = 0;
        }
        for(Index = 1; Index < 255; Index++) {
            Used += snprintf(Line + Used, sizeof(Line) - Used, ", \"x\"");
        }
        snprintf(Line + Used, sizeof(Line) - Used, " } },");
        CASE("strings-256", TooMany, 1, ASL({ KEY("mtp/compatible"), NULL }), NO_ASL, "mtp/compatible 512 value\n");
        CASE("strings-255", AsManyAsFit, 1, ASL({ KEY("mtp/compatible"), Line }));
    }

    // Words
    CASE("words-of-6-bytes", EDITS(EDIT_BYTES("dart-mtp", "remap", "\1\0\0\0\2\0")), ASL({ KEY("dart-mtp/remap"), NULL }), NO_ASL, "dart-mtp/remap 6 value\n");
    CASE("words-none", EDITS(EDIT_BYTES("dart-mtp", "remap", "")), ASL({ KEY("dart-mtp/remap"), NULL }), NO_ASL, "dart-mtp/remap 0 value\n");
    CASE("words-one", EDITS(EDIT_WORDS("dart-mtp", "remap", 0x01020304)), ASL({ KEY("dart-mtp/remap"), PROPERTY("dart-mtp/remap", "Package () { 0x01020304 }") }));

    // Lists that are empty on the M4
    CASE("lists",
         EDITS(EDIT_WORDS("mtp", "clock-gates", 0x12d, 5), EDIT_WORDS("mtp", "clock-ids", 7), EDIT_WORDS("mtp", "power-gates", 0x10000001),
               EDIT_WORDS("smc", "clock-gates", 1), EDIT_WORDS("smc", "clock-ids", 2, 3), EDIT_WORDS("smc", "power-gates", 4)),
         ASL({ KEY("mtp/iommu-stream"), PROPERTY("mtp/iommu-stream", "0") "\n" PROPERTY("mtp/clock-gates", "Package () { 0x12D, 5 }") "\n"
               PROPERTY("mtp/clock-ids", "Package () { 7 }") "\n" PROPERTY("mtp/power-gates", "Package () { 0x10000001 }") }),
         ASL({ KEY("smc/timers-reg-index"), PROPERTY("smc/timers-reg-index", "2") "\n" PROPERTY("smc/clock-gates", "Package () { 1 }") "\n"
               PROPERTY("smc/clock-ids", "Package () { 2, 3 }") "\n" PROPERTY("smc/power-gates", "Package () { 4 }") }));
    CASE("lists-left-out", EDITS(EDIT_NO_PROPERTY("mtp", "clock-gates"), EDIT_NO_PROPERTY("smc", "power-gates")));
    CASE("list-of-5-bytes", EDITS(EDIT_BYTES("mtp", "clock-gates", "\1\0\0\0\2")), NO_ASL, NO_ASL, "mtp/clock-gates 5 value\n");

    // References
    CASE("stream-elsewhere", EDITS(EDIT_WORDS("mapper-mtp", "reg", 5)),
         ASL({ KEY("mtp/iommu-stream"), PROPERTY("mtp/iommu-stream", "5") }, { KEY("mtp-transport/iommu-stream"), PROPERTY("mtp-transport/iommu-stream", "5") }));
    CASE("stream-of-another-node", EDITS(EDIT_WORDS("mtp", "iommu-parent", 0x10e), EDIT_WORDS("smc-charger-util-0", "AAPL,phandle", 0x10e), EDIT_WORDS("smc-charger-util-0", "reg", 9)),
         ASL({ KEY("mtp/iommu-stream"), PROPERTY("mtp/iommu-stream", "9") }));
    CASE("phandle-of-8-bytes", EDITS(EDIT_WORDS("smc-charger-util-0", "AAPL,phandle", 0x124, 0), EDIT_WORDS("smc-charger-util-0", "reg", 9)));
    CASE("stream-of-no-node", EDITS(EDIT_WORDS("mtp", "iommu-parent", 9999)), ASL({ KEY("mtp/iommu-stream"), NULL }), NO_ASL, "mtp/iommu-parent 4 reference\n");
    CASE("stream-of-8-bytes", EDITS(EDIT_WORDS("mtp", "iommu-parent", 0x124, 0x124)), ASL({ KEY("mtp/iommu-stream"), NULL }), NO_ASL, "mtp/iommu-parent 8 value\n");
    CASE("mapper-without-reg", EDITS(EDIT_NO_PROPERTY("mapper-mtp", "reg")), ASL({ KEY("mtp/iommu-stream"), NULL }, { KEY("mtp-transport/iommu-stream"), NULL }), NO_ASL,
         "mtp/iommu-parent 4 reference\n" "mtp-transport/iommu-parent 4 reference\n");
    CASE("mapper-with-reg-of-8-bytes", EDITS(EDIT_WORDS("mapper-mtp", "reg", 0, 0)), ASL({ KEY("mtp/iommu-stream"), NULL }, { KEY("mtp-transport/iommu-stream"), NULL }), NO_ASL,
         "mtp/iommu-parent 4 reference\n" "mtp-transport/iommu-parent 4 reference\n");
    CASE("function-of-another-node", EDITS(EDIT_WORDS("multi-touch", "function-afe-reset", 0x124, 0x61626364)),
         ASL({ KEY("multi-touch/function-afe-reset"), PROPERTY("multi-touch/function-afe-reset", "Package () { \"mapper-mtp\", 0x61626364 }") }));
    CASE("function-of-no-node", EDITS(EDIT_WORDS("multi-touch", "function-afe-reset", 9999, 0x61626364)), ASL({ KEY("multi-touch/function-afe-reset"), NULL }), NO_ASL,
         "multi-touch/function-afe-reset 8 reference\n");
    CASE("function-of-4-bytes", EDITS(EDIT_WORDS("multi-touch", "function-afe-reset", 0x10d)), ASL({ KEY("multi-touch/function-afe-reset"), NULL }), NO_ASL,
         "multi-touch/function-afe-reset 4 value\n");
    CASE("function-of-9-bytes", EDITS(EDIT_BYTES("stm", "function-stm-reset", "\x0d\1\0\0abcde")), ASL({ KEY("stm/function-stm-reset"), NULL }), NO_ASL,
         "stm/function-stm-reset 9 value\n");
    {
        UINT32          Many[256];
        CONST TEST_EDIT TooMany[]     = { { "multi-touch", "function-afe-reset", Many, sizeof(Many) } };
        UINTN           Index;

        Many[0] = 0x10d;
        for(Index = 1; Index < 256; Index++) {
            Many[Index] = MadeUpWord(Index);
        }
        CASE("function-of-256-elements", TooMany, 1, ASL({ KEY("multi-touch/function-afe-reset"), NULL }), NO_ASL, "multi-touch/function-afe-reset 1024 value\n");
        {
            CONST TEST_EDIT AsManyAsFit[] = { { "multi-touch", "function-afe-reset", Many, sizeof(Many) - 4 } };
            STATIC CHAR8    Line[8192];
            CHAR8           *Brace;

            WordsLine(Line, sizeof(Line), "multi-touch/function-afe-reset", 1, 254);
            Brace = strstr(Line, "Package () {") + sizeof("Package () {") - 1;
            memmove(Brace + sizeof(" \"smc-pmu\",") - 1, Brace, strlen(Brace) + 1);
            memcpy(Brace, " \"smc-pmu\",", sizeof(" \"smc-pmu\",") - 1);
            CASE("function-of-255-elements", AsManyAsFit, 1, ASL({ KEY("multi-touch/function-afe-reset"), Line }));
        }
    }

    // Long data
    CheckDapf("dapf-of-56-byte-entries", 126, 1);     // 504 bytes: nine entries
    CheckDapf("dapf-of-728-bytes", 182, 1);           // fourteen entries of 52 bytes or thirteen of 56
    CheckDapf("dapf-of-255-words", 255, 1);
    CheckDapf("dapf-of-256-words", 256, 2);
    CheckDapf("dapf-of-510-words", 510, 2);
    CheckDapf("dapf-of-511-words", 511, 3);
    CheckDapf("dapf-of-1000-words", 1000, 4);
    CASE("dapf-of-470-bytes", EDITS(EDIT_BYTES("dart-mtp", "dapf-instance-0", "\1\0\0\0\2\0")), ASL({ KEY("dart-mtp/dapf-instance-0"), NULL }), NO_ASL,
         "dart-mtp/dapf-instance-0 6 value\n");

    // Numbered
    CASE("more-instances",
         EDITS(EDIT_WORDS("dart-mtp", "dapf-instance-1", 1, 2), EDIT_WORDS("dart-mtp", "dapf-instance-63", 3), EDIT_WORDS("dart-mtp", "dapf-instance-64", 4),
               EDIT_WORDS("dart-mtp", "dart-tunables-instance-2", 5, 6), EDIT_WORDS("dart-mtp", "dart-tunables-instance-10", 7)),
         ASL({ "0x310, 4, 0x3FFFFFFC", PROPERTY("dart-mtp/dart-tunables-instance-0", "Package () { 0x20C, 4, 0x80000121, 0, 0x80000121, 0, 0x220, 4, 0xF0F0F, 0, 0xF0F0F, 0, "
               "0x224, 4, 0xFFFFFF, 0, 0x80808, 0, 0x300, 4, 0x1F31, 0, 1, 0, 0x308, 4, 0x3FFFFFFC, 0, 0x10000000, 0, 0x310, 4, 0x3FFFFFFC, 0, 0x3FFFFFFC, 0 }") "\n"
               PROPERTY("dart-mtp/dart-tunables-instance-2", "Package () { 5, 6 }") "\n" PROPERTY("dart-mtp/dart-tunables-instance-10", "Package () { 7 }") },
             { KEY("dockchannel-mtp/compatible"), PROPERTY("dart-mtp/dapf-instance-1", "Package () { 1, 2 }") "\n" PROPERTY("dart-mtp/dapf-instance-63", "Package () { 3 }") "\n"
               PROPERTY("dockchannel-mtp/compatible", "Package () { \"dockchannel,t8002\" }") }));
    CASE("more-events", EDITS(EDIT_BYTES("smc-pmu", "event_name-bit1", "one\0"), EDIT_BYTES("smc-pmu", "event_name-bit63", "last\0"), EDIT_BYTES("smc-pmu", "event_name-bit64", "x\0")), NO_ASL,
         ASL({ KEY("smc-pmu/event_name-bit0"), PROPERTY("smc-pmu/event_name-bit0", "\"pwrbtn\"") "\n" PROPERTY("smc-pmu/event_name-bit1", "\"one\"") },
             { KEY("smc-pmu/event_name-bit48"), PROPERTY("smc-pmu/event_name-bit48", "\"rtc\"") "\n" PROPERTY("smc-pmu/event_name-bit63", "\"last\"") }));

    // A property that the nodes do not hold changes nothing
    CASE("unknown-property", EDITS(EDIT_WORDS("mtp", "something-new", 1), EDIT_WORDS("smc-pmu", "event_name", 1)));
}

//
// Children: how many there are is the tree's business
//
STATIC VOID CheckChildren(VOID)
{
    CONST TEST_PROPERTY Charger[]  = { TEST_TEXT ("device_type", "smc-charger-util"), TEST_WORDS ("port-type", 3), TEST_WORDS ("dock-num", 9) };
    CONST TEST_PROPERTY Other[]    = { TEST_TEXT ("device_type", "smc-charger"), TEST_WORDS ("dock-num", 9) };
    CONST TEST_PROPERTY NoType[]   = { TEST_WORDS ("dock-num", 9) };
    CONST TEST_PROPERTY BadType[]  = { TEST_WORDS ("device_type", 0x2d636d73), TEST_WORDS ("dock-num", 9) };
    TEST_MACHINE               Machine;

    {
        CONST TEST_NODE Children[] = {
            TEST_NODE_OF (2, "another", Charger), TEST_NODE_OF (3, "below-it", Charger), TEST_NODE_OF (2, "of-another-type", Other),
            TEST_NODE_OF (2, "of-no-type", NoType), TEST_NODE_OF (2, "of-a-type-that-is-no-string", BadType),
        };

        Machine = WithChildren("smc-charger-util", Children, ARRAY_SIZE(Children));
        CASE("another-charger-util", NULL, 0, NO_ASL,
             ASL({ KEY("smc-charger-util/magsafe-auth"), PROPERTY("smc-charger-util/magsafe-auth", "1") "\n" PROPERTY("another/dock-num", "9") "\n" PROPERTY("another/port-type", "3") }),
             .Machine = &Machine);
    }
    CASE("another-type-of-charger", EDITS(EDIT_BYTES("smc-charger-util-1", "device_type", "smc-charger-utilx\0")), NO_ASL,
         ASL({ KEY("smc-charger-util-1/dock-num"), NULL }, { KEY("smc-charger-util-1/port-number"), NULL }, { KEY("smc-charger-util-1/port-type"), NULL }));
    CASE("charger-of-wrong-length", EDITS(EDIT_WORDS("smc-charger-util-3", "port-type", 2, 2)), NO_ASL, ASL({ KEY("smc-charger-util-3/port-type"), NULL }), NULL,
         "smc-charger-util-3/port-type 8 value\n");

    //
    // Below the interfaces there may be more; only the children are interfaces. One
    // whose name is no string is none.
    //
    {
        CONST TEST_NODE Children[] = { TEST_NODE_OF (3, "below-the-keyboard", NoType), TEST_NODE_OF (2, "new", NoType), TEST_NODE_OF (2, "\x80", NoType) };

        Machine = WithChildren("keyboard", Children, ARRAY_SIZE(Children));
        CASE("another-interface", NULL, 0,
             ASL({ KEY("mtp-transport/interfaces"), PROPERTY("mtp-transport/interfaces", "Package () { \"multi-touch\", \"keyboard\", \"new\", \"stm\", \"actuator\", \"tp-accel\" }") }),
             .Machine = &Machine);
    }

    //
    // More than there is room for: 255 properties in all, the rest is skipped and
    // counted. Four for each child.
    //
    {
        CONST TEST_PROPERTY Full[] = { TEST_TEXT ("device_type", "smc-charger-util"), TEST_WORDS ("dock-num", 1), TEST_WORDS ("port-number", 2), TEST_WORDS ("port-type", 3),
                                              TEST_WORDS ("magsafe-auth", 4) };
        STATIC TEST_NODE           Children[60];
        STATIC CHAR8               Names[60][16];
        STATIC CHAR8               Lines[65536];
        STATIC CHAR8               Skipped[4096];
        STATIC CONST CHAR8 *CONST  Keys[] = { "dock-num", "port-number", "port-type", "magsafe-auth" };
        UINTN                      Used = snprintf(Lines, sizeof(Lines), "%s", PROPERTY("smc-charger-util/magsafe-auth", "1"));
        UINTN                      UsedSkipped = 0;
        UINTN                      Total = 46;    // the nodes' own
        UINTN                      SkippedTotal = 0;
        UINTN                      Index;

        for(Index = 0; Index < 4 * ARRAY_SIZE(Children); Index++) {
            if(Index % 4 == 0) {
                snprintf(Names[Index / 4], sizeof(Names[0]), "more-%u", (unsigned)Index / 4);
                Children[Index / 4] = (TEST_NODE) { 2, Names[Index / 4], Full, ARRAY_SIZE(Full) };
            }
            if(Total < 255) {
                Total++;
                Used += snprintf(Lines + Used, sizeof(Lines) - Used, "\n                Package (2) { \"%s/%s\", %u },", Names[Index / 4], Keys[Index % 4], (unsigned)(Index % 4 + 1));
            } else {
                if(SkippedTotal < DEVICE_MAX_SKIPPED) {
                    UsedSkipped += snprintf(Skipped + UsedSkipped, sizeof(Skipped) - UsedSkipped, "%s/%s 4 room\n", Names[Index / 4], Keys[Index % 4]);
                }
                SkippedTotal++;
            }
        }
        if(SkippedTotal > DEVICE_MAX_SKIPPED) {
            snprintf(Skipped + UsedSkipped, sizeof(Skipped) - UsedSkipped, "%u in all\n", (unsigned)SkippedTotal);
        }
        Machine = WithChildren("smc-charger-util", Children, ARRAY_SIZE(Children));
        CASE("more-than-255-properties", NULL, 0, NO_ASL, ASL({ KEY("smc-charger-util/magsafe-auth"), Lines }), NULL, Skipped, .Machine = &Machine);
        Expect(mSmc.Dsdt.PropertyCount == 255, "more-than-255-properties: there are 255");
        Expect(SkippedTotal == 31, "more-than-255-properties: 31 are skipped");
    }

    //
    // A long property of which only some parts would fit in is skipped whole
    //
    {
        CONST TEST_PROPERTY Full[] = { TEST_WORDS ("reg", 0) };
        STATIC TEST_NODE           Children[300];
        STATIC CHAR8               Names[300][8];
        UINTN                      Index;

        for(Index = 0; Index < ARRAY_SIZE(Children); Index++) {
            snprintf(Names[Index], sizeof(Names[0]), "i%u", (unsigned)Index);
            Children[Index] = (TEST_NODE) { 2, Names[Index], Full, ARRAY_SIZE(Full) };
        }
        Machine = WithChildren("tp-accel", Children, 251);
        CASE("256-interfaces", NULL, 0, ASL({ KEY("mtp-transport/interfaces"), NULL }), NO_ASL, "mtp-transport/interfaces 0 room\n", .Machine = &Machine);
        Machine = WithChildren("tp-accel", Children, 250);
        {
            STATIC CHAR8 Line[8192];
            UINTN        Used = snprintf(Line, sizeof(Line), "                Package (2) { \"mtp-transport/interfaces\", Package () { \"multi-touch\", \"keyboard\", \"stm\", \"actuator\", \"tp-accel\"");

            for(Index = 0; Index < 250; Index++) {
                Used += snprintf(Line + Used, sizeof(Line) - Used, ", \"%s\"", Names[Index]);
            }
            snprintf(Line + Used, sizeof(Line) - Used, " } },");
            CASE("255-interfaces", NULL, 0, ASL({ KEY("mtp-transport/interfaces"), Line }), .Machine = &Machine);
        }
    }

    //
    // No room for the names: they are skipped whole, and what comes after them is not
    //
    {
        CONST TEST_PROPERTY Full[] = { TEST_WORDS ("reg", 0) };
        STATIC TEST_NODE           Children[200];
        STATIC CHAR8               Names[200][200];
        UINTN                      Index;

        for(Index = 0; Index < ARRAY_SIZE(Children); Index++) {
            memset(Names[Index], 'a', sizeof(Names[0]));
            snprintf(Names[Index] + 190, 8, "%u", (unsigned)Index);
            Children[Index] = (TEST_NODE) { 2, Names[Index], Full, ARRAY_SIZE(Full) };
        }
        Machine = WithChildren("tp-accel", Children, ARRAY_SIZE(Children));
        CASE("too-long-interfaces", NULL, 0, ASL({ KEY("mtp-transport/interfaces"), NULL }), NO_ASL, "mtp-transport/interfaces 0 room\n", .Machine = &Machine);
    }
}

//
// A long property that does not fit in whole any more is skipped whole: no part of it
//
STATIC VOID CheckPartsThatDoNotFit(VOID)
{
    STATIC DEVICE_PROPERTIES    Properties;
    STATIC CONST PROPERTY_ROW   Rows[] = { { "dapf-instance-", PropertyWords, TRUE }, { "page-size", PropertyInteger } };
    STATIC CONST PROPERTY_NODE  Node[] = { { "/arm-io/dart-mtp", NULL, Rows, ARRAY_SIZE(Rows) } };
    UINT32                      Words[256] = { 0 };
    CONST TEST_EDIT             Edits[] = { { "dart-mtp", "dapf-instance-0", Words, sizeof(Words) } };
    TEST_MACHINE                Machine = mM4MacBookProWhole;

    Machine.Edits     = Edits;
    Machine.EditCount = ARRAY_SIZE(Edits);
    BuildMachine(&Machine);

    memset(&Properties, 0, sizeof(Properties));
    Properties.Count = 253;
    CopyProperties(BuiltTree()->DeviceTree, Node, ARRAY_SIZE(Node), &Properties);
    Expect(Properties.Count == 255 && Properties.SkippedCount == 1 && strcmp(DumpSkipped(&Properties), "dart-mtp/page-size 4 room\n") == 0 &&
           strcmp(Properties.Properties[253].Key, "dart-mtp/dapf-instance-0") == 0 && strcmp(Properties.Properties[254].Key, "dart-mtp/dapf-instance-0+1") == 0,
           "a property of two parts, room for two");

    memset(&Properties, 0, sizeof(Properties));
    Properties.Count = 254;
    CopyProperties(BuiltTree()->DeviceTree, Node, ARRAY_SIZE(Node), &Properties);
    Expect(Properties.Count == 255 && Properties.SkippedCount == 1 && strcmp(DumpSkipped(&Properties), "dart-mtp/dapf-instance-0 1024 room\n") == 0 &&
           strcmp(Properties.Properties[254].Key, "dart-mtp/page-size") == 0, "a property of two parts, room for one: skipped whole, the next one is taken");
}

//
// A whole device tree, the one iBoot handed over on a machine, not cut down; and the
// ASL of the two nodes that was made from it by other means. Neither is in this
// repository. The table of interrupt numbers is put in as the loader does it.
//
STATIC VOID CheckRealTree(IN CONST CHAR8 *TreeFile, IN CONST CHAR8 *AslFile)
{
    STATIC UINT8       Tree[2 * 1024 * 1024] __attribute__((aligned(8)));
    STATIC CHAR8       Asl[262144];
    FILE               *File;
    UINTN              TreeSize;
    UINTN              AslSize;
    UINTN              Used;
    UINTN              Size;
    APPLE_BOOT_INFO    Info = *BuiltTree();
    UINT64             Entry[2] = { (UINT64)(UINTN)mGuestMemory, 8 + 4 * ARRAY_SIZE(mAicToGicOfM4MacBookPro) };
    UINT32             *Table   = (UINT32 *)mGuestMemory;
    UINT8              *Reserved;
    ANS_NODE           Ans;
    CONST DSDT_DEVICE  *Devices[2] = { &mMtp.Dsdt, &mSmc.Dsdt };

    File     = fopen(TreeFile, "rb");
    TreeSize = File != NULL ? fread(Tree, 1, sizeof(Tree), File) : 0;
    if(File != NULL) {
        fclose(File);
    }
    Info.DeviceTree     = (dt_node_t *)Tree;
    Info.DeviceTreeSize = (UINT32)TreeSize;
    if(!Expect(TreeSize != 0 && dt_check(Tree, TreeSize, NULL) == 0 && dt_find(Info.DeviceTree, "/chosen/memory-map") != NULL, "whole tree: it is a device tree")) {
        return;
    }
    Reserved = dt_prop(dt_find(Info.DeviceTree, "/chosen/memory-map"), "MemoryMapReserved-0", NULL);
    if(!Expect(Reserved != NULL, "whole tree: there is room for AICToGIC")) {
        return;
    }
    memset(Reserved - 4 - DT_KEY_LEN, 0, DT_KEY_LEN);
    strcpy((CHAR8 *)Reserved - 4 - DT_KEY_LEN, "AICToGIC");
    memcpy(Reserved, Entry, sizeof(Entry));
    Table[0] = APPLE_AIC_TO_GIC_VERSION;
    Table[1] = ARRAY_SIZE(mAicToGicOfM4MacBookPro);
    memcpy(&Table[2], mAicToGicOfM4MacBookPro, sizeof(mAicToGicOfM4MacBookPro));

    FindAnsNode(&Info, &Ans);
    Expect(Ans.Outcome == AnsNodeMade && SameNode(&Ans.Node, &mM4Node), "whole tree: the disk controller as before");
    FindNodes(&Info);
    if(!Expect(mMtp.Outcome == CoprocessorNodeMade && mSmc.Outcome == CoprocessorNodeMade && mMtp.Properties.SkippedCount == 0 && mSmc.Properties.SkippedCount == 0 &&
               mMtp.Dsdt.PropertyCount == 48 && mSmc.Dsdt.PropertyCount == 46, "whole tree: both nodes, 48 and 46 properties, none skipped")) {
        return;
    }
    memset(mTable, 0xa5, sizeof(mTable));
    Size = DsdtSize(NULL, 0, Devices, 2);
    if(!Expect(Size < sizeof(mTable) && DsdtBuild(0x8132, CREATOR_REVISION, NULL, 0, Devices, 2, mTable) == RETURN_SUCCESS && mTable[Size] == 0xa5, "whole tree: the DSDT is built")) {
        return;
    }
    printf("ok    whole tree: %u bytes of DSDT\n", (unsigned)Size);
    WriteOutput("whole-tree", "aml", mTable, Size);

    Used    = snprintf(Asl, sizeof(Asl), "DefinitionBlock (\"\", \"DSDT\", 2, \"Apple \", \"APPLEEFI\", 0x8132) {\n  Scope (\\_SB) {\n");
    File    = fopen(AslFile, "rb");
    AslSize = File != NULL ? fread(Asl + Used, 1, sizeof(Asl) - Used - 16, File) : 0;
    if(File != NULL) {
        fclose(File);
    }
    Used += AslSize;
    Used += snprintf(Asl + Used, sizeof(Asl) - Used, "\n  }\n}\n");
    Expect(AslSize != 0, "whole tree: the ASL of the nodes is read");
    WriteOutput("whole-tree", "asl", Asl, Used);
}

STATIC VOID CheckCoprocessorNodes(VOID)
{
    ReadAsl("../MtpNode.asl", mMtpAsl, sizeof(mMtpAsl));
    ReadAsl("../SmcNode.asl", mSmcAsl, sizeof(mSmcAsl));

    CheckResources();
    CheckNoNode();
    CheckEveryPropertyLeftOut(mMtpAsl, FALSE);
    CheckEveryPropertyLeftOut(mSmcAsl, TRUE);
    CheckNodesLeftOut();
    CheckValues();
    CheckChildren();
    CheckPartsThatDoNotFit();
}
