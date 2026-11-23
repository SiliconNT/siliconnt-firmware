/** @file
  Host test of AppleGetAns(), AppleGetAnsSartVersion() and AppleGetAnsPmgr() in
  Library/AppleDTLib: what the firmware reads about the internal disk controller,
  /arm-io/ans and /arm-io/sart-ans, the version of its SART, and where its
  register in the power manager is.

  Builds Apple device trees in memory, the one of the M4 MacBook Pro among them,
  and checks what the function makes of them. Run it with ./run.sh; it needs the
  host compiler only.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "HostTest.h"

//
// What AppleGetAns() hands back for the M4 MacBook Pro: entry 9 of "reg" with
// the length of entry 3, entry 0, entry 0 of sart-ans, all moved by 0x2_0000_0000
// (the first range of /arm-io); AIC 1018 (value 4) and AIC 1006 (value 2).
//
STATIC CONST APPLE_ANS mM4Ans = {
    { 0x4c5cc0000ULL, 0x60000 },
    { 0x481600000ULL, 0x88000 },
    { 0x485c50000ULL, 0xc000 },
    1018, 1006, TRUE
};

//
// A made-up machine with another shape: two ranges, the coprocessor in the
// second one, four "reg" entries, no "nvme-secure-bar", six interrupt lines with
// the controller's last, a SART of version 2 and no "compatible", no
// "clock-gates".
//
STATIC CONST UINT64 mOtherRanges[][3] = {
    { 0x000000000ULL, 0x200000000ULL, 0x100000000ULL },
    { 0x100000000ULL, 0x500000000ULL, 0x010000000ULL },
};

STATIC CONST UINT64 mOtherReg[][2] = {
    { 0x100400000ULL, 0x4000 },
    { 0x0a0000000ULL, 0x4000 },
    { 0x0ULL, 0x0 },
    { 0x093cc0000ULL, 0x40000 },
};

STATIC CONST UINT64 mOtherSartReg[][2] = {
    { 0x093c50000ULL, 0x10000 },
};

STATIC CONST UINT32 mOtherInterrupts[] = { 1070, 1069, 1072, 1071, 7, 1613 };

STATIC CONST UINT32 mOtherSartVersion = 2;

STATIC CONST TEST_ANS mOtherAns = {
    mOtherReg, sizeof(mOtherReg),
    mOtherInterrupts, sizeof(mOtherInterrupts),
    5, FALSE,
    mOtherSartReg, sizeof(mOtherSartReg),
    OMIT_ANS_CLOCK_GATES,
    &mOtherSartVersion, sizeof(mOtherSartVersion),
    NULL, 0,
    NULL, 0
};

STATIC CONST APPLE_ANS mOtherExpected = {
    { 0x293cc0000ULL, 0x40000 },
    { 0x500400000ULL, 0x4000 },
    { 0x293c50000ULL, 0x10000 },
    1613, 1072, FALSE
};

STATIC BOOLEAN SameAns(IN CONST APPLE_ANS *A, IN CONST APPLE_ANS *B)
{
    return A->Nvme.Base == B->Nvme.Base && A->Nvme.Size == B->Nvme.Size &&
           A->Coprocessor.Base == B->Coprocessor.Base && A->Coprocessor.Size == B->Coprocessor.Size &&
           A->Sart.Base == B->Sart.Base && A->Sart.Size == B->Sart.Size &&
           A->NvmeAic == B->NvmeAic && A->MailboxAic == B->MailboxAic && A->SecureBar == B->SecureBar;
}

//
// Reads the controller of Machine. Expected NULL: the status is not
// RETURN_SUCCESS and everything handed back is 0.
//
STATIC VOID Check(IN CONST CHAR8 *What, IN CONST TEST_MACHINE *Machine, IN RETURN_STATUS ExpectedStatus, IN CONST APPLE_ANS *Expected)
{
    STATIC CONST APPLE_ANS Zero;
    APPLE_ANS              Ans;
    RETURN_STATUS          Status;

    BuildMachine(Machine);
    memset(&Ans, 0x5a, sizeof(Ans));
    Status = AppleGetAns(BuiltTree(), &Ans);
    if(!Expect(dt_check(mTree, mUsed, NULL) == 0 && Status == ExpectedStatus && SameAns(&Ans, Expected != NULL ? Expected : &Zero), What)) {
        printf("      status 0x%llx nvme 0x%llx+0x%llx coprocessor 0x%llx+0x%llx sart 0x%llx+0x%llx aic %u %u secure-bar %d\n",
               (unsigned long long)Status, (unsigned long long)Ans.Nvme.Base, (unsigned long long)Ans.Nvme.Size,
               (unsigned long long)Ans.Coprocessor.Base, (unsigned long long)Ans.Coprocessor.Size,
               (unsigned long long)Ans.Sart.Base, (unsigned long long)Ans.Sart.Size,
               (unsigned)Ans.NvmeAic, (unsigned)Ans.MailboxAic, (int)Ans.SecureBar);
    }
}

// The M4 MacBook Pro with another disk controller
STATIC VOID CheckM4With(IN CONST CHAR8 *What, IN CONST TEST_ANS *Ans, IN RETURN_STATUS ExpectedStatus, IN CONST APPLE_ANS *Expected)
{
    TEST_MACHINE Machine = mM4MacBookProWithAns;

    Machine.Ans = Ans;
    Check(What, &Machine, ExpectedStatus, Expected);
}

// The controller of the M4 MacBook Pro without what Omit names
STATIC VOID CheckM4Without(IN CONST CHAR8 *What, IN UINT32 Omit, IN RETURN_STATUS ExpectedStatus)
{
    TEST_ANS Ans = mAnsOfM4MacBookPro;

    Ans.Omit = Omit;
    CheckM4With(What, &Ans, ExpectedStatus, NULL);
}

// The controller of the M4 MacBook Pro with a copy of its "reg" in which entry Entry is Address, Size
STATIC VOID CheckM4Reg(IN CONST CHAR8 *What, IN UINTN Entry, IN UINT64 Address, IN UINT64 Size, IN RETURN_STATUS ExpectedStatus, IN CONST APPLE_ANS *Expected)
{
    UINT64   Reg[ARRAY_SIZE(mAnsRegOfM4MacBookPro)][2];
    TEST_ANS Ans = mAnsOfM4MacBookPro;

    memcpy(Reg, mAnsRegOfM4MacBookPro, sizeof(Reg));
    Reg[Entry][0] = Address;
    Reg[Entry][1] = Size;
    Ans.Reg       = (CONST UINT64 (*)[2])Reg;
    CheckM4With(What, &Ans, ExpectedStatus, Expected);
}

//
// The power manager: reads the controller of Machine, which must succeed, then
// its register. ExpectedBase 0: the status is not RETURN_SUCCESS and the range
// handed back is all 0.
//
STATIC VOID CheckPmgr(IN CONST CHAR8 *What, IN CONST TEST_MACHINE *Machine, IN RETURN_STATUS ExpectedStatus, IN UINT64 ExpectedBase)
{
    APPLE_ANS        Ans;
    APPLE_MMIO_RANGE Pmgr;
    RETURN_STATUS    Status;

    BuildMachine(Machine);
    memset(&Pmgr, 0x5a, sizeof(Pmgr));
    Status = AppleGetAns(BuiltTree(), &Ans);
    if(Status == RETURN_SUCCESS) {
        Status = AppleGetAnsPmgr(BuiltTree(), &Ans, &Pmgr);
    }
    if(!Expect(dt_check(mTree, mUsed, NULL) == 0 && Status == ExpectedStatus && Pmgr.Base == ExpectedBase &&
               Pmgr.Size == (ExpectedBase != 0 ? 4 : 0), What)) {
        printf("      status 0x%llx pmgr 0x%llx+0x%llx\n", (unsigned long long)Status, (unsigned long long)Pmgr.Base, (unsigned long long)Pmgr.Size);
    }
}

// The M4 MacBook Pro with another power manager
STATIC VOID CheckM4Pmgr(IN CONST CHAR8 *What, IN CONST TEST_PMGR *Pmgr, IN RETURN_STATUS ExpectedStatus, IN UINT64 ExpectedBase)
{
    TEST_MACHINE Machine = mM4MacBookProWithAns;

    Machine.Pmgr = Pmgr;
    CheckPmgr(What, &Machine, ExpectedStatus, ExpectedBase);
}

// The power manager of the M4 MacBook Pro with Devices for "devices"
STATIC VOID CheckM4Devices(IN CONST CHAR8 *What, IN CONST UINT8 *Devices, IN UINT32 Length, IN RETURN_STATUS ExpectedStatus, IN UINT64 ExpectedBase)
{
    TEST_PMGR Pmgr = mPmgrOfM4MacBookPro;

    Pmgr.Devices       = Devices;
    Pmgr.DevicesLength = Length;
    CheckM4Pmgr(What, &Pmgr, ExpectedStatus, ExpectedBase);
}

// The M4 MacBook Pro with another controller and its power manager
STATIC VOID CheckM4AnsPmgr(IN CONST CHAR8 *What, IN CONST TEST_ANS *Ans, IN RETURN_STATUS ExpectedStatus, IN UINT64 ExpectedBase)
{
    TEST_MACHINE Machine = mM4MacBookProWithAns;

    Machine.Ans = Ans;
    CheckPmgr(What, &Machine, ExpectedStatus, ExpectedBase);
}

#define M4_PMGR_ANS  0x380700538ULL     // "reg" entry 0 for the CPU + 0x500 + 7 * 8
#define DIE_1        0x2000000000ULL

// The version of the SART of Machine; with any status but RETURN_SUCCESS it must be 0
STATIC VOID CheckSart(IN CONST CHAR8 *What, IN CONST TEST_MACHINE *Machine, IN RETURN_STATUS ExpectedStatus, IN UINT32 ExpectedVersion)
{
    UINT32        Version = 0x5a5a5a5a;
    RETURN_STATUS Status;

    BuildMachine(Machine);
    Status = AppleGetAnsSartVersion(BuiltTree(), &Version);
    if(!Expect(dt_check(mTree, mUsed, NULL) == 0 && Status == ExpectedStatus && Version == ExpectedVersion, What)) {
        printf("      status 0x%llx version 0x%x\n", (unsigned long long)Status, (unsigned)Version);
    }
}

STATIC VOID CheckM4Sart(IN CONST CHAR8 *What, IN CONST TEST_ANS *Ans, IN RETURN_STATUS ExpectedStatus, IN UINT32 ExpectedVersion)
{
    TEST_MACHINE Machine = mM4MacBookProWithAns;

    Machine.Ans = Ans;
    CheckSart(What, &Machine, ExpectedStatus, ExpectedVersion);
}

STATIC VOID CheckSartVersions(VOID)
{
    STATIC CONST CHAR8  T8015[]       = "sart,t8015";
    STATIC CONST CHAR8  T8015Second[] = "apple,sart\0sart,t8015";
    STATIC CONST CHAR8  Longer[]      = "sart,t8015x";
    STATIC CONST CHAR8  Shorter[]     = "sart,t801";
    STATIC CONST UINT16 Short         = 3;
    STATIC CONST UINT32 Two[2]        = { 3, 3 };
    STATIC CONST UINT32 Large         = 0x12345678;
    TEST_ANS            Ans           = mAnsOfM4MacBookPro;
    TEST_MACHINE        Machine;

    CheckSart("M4 MacBook Pro: SART version 3", &mM4MacBookProWithAns, RETURN_SUCCESS, 3);
    Ans.SartVersion = &Large;
    CheckM4Sart("\"sart-version\" is passed on as the tree has it", &Ans, RETURN_SUCCESS, 0x12345678);

    Ans.Omit = OMIT_SART_VERSION;
    CheckM4Sart("no \"sart-version\", \"sart,coastguard\"", &Ans, RETURN_NOT_FOUND, 0);
    Ans.SartCompatible       = T8015;
    Ans.SartCompatibleLength = sizeof(T8015);
    CheckM4Sart("no \"sart-version\", \"sart,t8015\": version 0", &Ans, RETURN_SUCCESS, 0);
    Ans.SartCompatible       = T8015Second;
    Ans.SartCompatibleLength = sizeof(T8015Second);
    CheckM4Sart("no \"sart-version\", \"sart,t8015\" the second string of \"compatible\": version 0", &Ans, RETURN_SUCCESS, 0);
    Ans.SartCompatibleLength = sizeof(T8015Second) - 1;
    CheckM4Sart("the same without the last zero: a string cut off by the end of the property does not count", &Ans, RETURN_NOT_FOUND, 0);
    Ans.SartCompatible       = Longer;
    Ans.SartCompatibleLength = sizeof(Longer);
    CheckM4Sart("no \"sart-version\", \"sart,t8015x\"", &Ans, RETURN_NOT_FOUND, 0);
    Ans.SartCompatible       = Shorter;
    Ans.SartCompatibleLength = sizeof(Shorter);
    CheckM4Sart("no \"sart-version\", \"sart,t801\"", &Ans, RETURN_NOT_FOUND, 0);
    Ans.SartCompatible       = NULL;
    Ans.SartCompatibleLength = 0;
    CheckM4Sart("no \"sart-version\", no \"compatible\"", &Ans, RETURN_NOT_FOUND, 0);

    Ans                      = mAnsOfM4MacBookPro;
    Ans.SartCompatible       = T8015;
    Ans.SartCompatibleLength = sizeof(T8015);
    CheckM4Sart("\"sart-version\" 3 and \"sart,t8015\": the property wins", &Ans, RETURN_SUCCESS, 3);
    Ans                   = mAnsOfM4MacBookPro;
    Ans.SartVersion       = &Short;
    Ans.SartVersionLength = sizeof(Short);
    CheckM4Sart("a \"sart-version\" of two bytes", &Ans, RETURN_INVALID_PARAMETER, 0);
    Ans.SartVersion       = Two;
    Ans.SartVersionLength = sizeof(Two);
    CheckM4Sart("a \"sart-version\" of two values", &Ans, RETURN_INVALID_PARAMETER, 0);
    Ans.SartVersionLength = 0;
    CheckM4Sart("an empty \"sart-version\"", &Ans, RETURN_INVALID_PARAMETER, 0);

    Ans.Omit = OMIT_SART;
    CheckM4Sart("no /arm-io/sart-ans", &Ans, RETURN_NOT_FOUND, 0);
    CheckSart("no disk controller at all", &mM4MacBookPro, RETURN_NOT_FOUND, 0);

    // What AppleGetAns() reads does not change with the version
    Ans             = mAnsOfM4MacBookPro;
    Ans.Omit        = OMIT_SART_VERSION;
    Machine         = mM4MacBookProWithAns;
    Machine.Ans     = &Ans;
    Check("no \"sart-version\": AppleGetAns() reads the same", &Machine, RETURN_SUCCESS, &mM4Ans);
}

// A copy of the M4's "devices" with entry Index given Name and Flags
STATIC UINT8 *DevicesWith(OUT UINT8 *Devices, IN UINTN Index, IN CONST CHAR8 *Name, IN UINT8 Flags)
{
    memcpy(Devices, mPmgrDevicesOfM4MacBookPro, sizeof(mPmgrDevicesOfM4MacBookPro));
    memset(Devices + Index * 48 + 32, 0, 16);
    memcpy(Devices + Index * 48 + 32, Name, strlen(Name));
    Devices[Index * 48] = Flags;
    return Devices;
}

STATIC VOID CheckPmgrs(VOID)
{
    STATIC CONST CHAR8  Pmgr2[]       = "pmgr2,t8152\0pmgr2,arch";
    STATIC CONST UINT32 DieOne        = 0x10000094;   // die 1, device 148
    STATIC CONST UINT32 DieOneTwice[] = { 0x10000094, 0x00000095 };
    STATIC CONST UINT64 TopReg[1][2]  = { { 0xfffffffffffff000ULL, 0x1000 } };
    UINT8               Devices[sizeof(mPmgrDevicesOfM4MacBookPro)];
    UINT8               Twice[sizeof(mPmgrDevicesOfM4MacBookPro) + 48];
    UINT32              Groups[ARRAY_SIZE(mPmgrPsRegsOfM4MacBookPro)];
    UINT64              Reg[ARRAY_SIZE(mPmgrRegOfM4MacBookPro)][2];
    UINT64              AnsReg[ARRAY_SIZE(mAnsRegOfM4MacBookPro)][2];
    UINT32              GroupAndOffset;
    TEST_PMGR           Pmgr;
    TEST_ANS            Ans;
    TEST_MACHINE        Machine;

    //
    // Found
    //
    CheckPmgr("M4 MacBook Pro: the controller's register in the power manager is 0x3_8070_0538", &mM4MacBookProWithAns, RETURN_SUCCESS, M4_PMGR_ANS);

    CheckM4Devices("the entry named ANS2 instead", DevicesWith(Devices, PMGR_DEVICE_INDEX_ANS, "ANS2", 0x20), sizeof(Devices), RETURN_SUCCESS, M4_PMGR_ANS);
    CheckM4Devices("a virtual entry named ANS before the real one does not count", DevicesWith(Devices, 1, "ANS", 0x12), sizeof(Devices), RETURN_SUCCESS, M4_PMGR_ANS);
    memcpy(Twice, mPmgrDevicesOfM4MacBookPro, sizeof(mPmgrDevicesOfM4MacBookPro));
    memcpy(Twice + sizeof(mPmgrDevicesOfM4MacBookPro), mPmgrDevicesOfM4MacBookPro + 48, 48);
    memcpy(Twice + sizeof(mPmgrDevicesOfM4MacBookPro) + 32, "ANS\0", 4);
    CheckM4Devices("two entries named ANS: the first", Twice, sizeof(Twice), RETURN_SUCCESS, M4_PMGR_ANS);

    // "ps-groups" instead of "ps-regs": group 5, 0x38 into it, in the entry's 32 bits at byte 16
    memcpy(Devices, mPmgrDevicesOfM4MacBookPro, sizeof(Devices));
    memset(Devices + PMGR_DEVICE_INDEX_ANS * 48 + 10, 0, 2);
    GroupAndOffset = (5U << 24) | 0x38;
    memcpy(Devices + PMGR_DEVICE_INDEX_ANS * 48 + 16, &GroupAndOffset, 4);
    Pmgr               = mPmgrOfM4MacBookPro;
    Pmgr.ByGroup       = TRUE;
    Pmgr.Devices       = Devices;
    CheckM4Pmgr("\"ps-groups\": the group and the offset from the entry's 32 bits at byte 16", &Pmgr, RETURN_SUCCESS, M4_PMGR_ANS);
    Pmgr.GroupsLength  = 0;
    CheckM4Pmgr("\"ps-groups\" empty", &Pmgr, RETURN_NOT_FOUND, 0);
    Pmgr               = mPmgrOfM4MacBookPro;
    Pmgr.GroupsLength  = 0;
    CheckM4Pmgr("\"ps-regs\" empty and no \"ps-groups\"", &Pmgr, RETURN_NOT_FOUND, 0);

    // /arm-io/pmgr says "pmgr2,arch": the power manager is /arm-io/pmgr-child
    Machine                       = mM4MacBookProWithAns;
    Pmgr                          = mPmgrOfM4MacBookPro;
    Pmgr.Compatible               = Pmgr2;
    Pmgr.CompatibleLength         = sizeof(Pmgr2);
    Pmgr.Omit                     = OMIT_PMGR_DEVICES;
    Machine.Pmgr                  = &Pmgr;
    Machine.PmgrChild             = &mPmgrOfM4MacBookPro;
    CheckPmgr("\"pmgr2,arch\": /arm-io/pmgr-child is the power manager", &Machine, RETURN_SUCCESS, M4_PMGR_ANS);
    Machine.PmgrChild             = NULL;
    CheckPmgr("\"pmgr2,arch\" and no /arm-io/pmgr-child", &Machine, RETURN_NOT_FOUND, 0);
    Machine.Pmgr                  = &mPmgrOfM4MacBookPro;
    Pmgr.Compatible               = "pmgr1,t8132";
    Pmgr.CompatibleLength         = sizeof("pmgr1,t8132");
    Pmgr.Omit                     = 0;
    Pmgr.Devices                  = DevicesWith(Devices, PMGR_DEVICE_INDEX_ANS, "ANX", 0x20);
    Machine.PmgrChild             = &Pmgr;
    CheckPmgr("without \"pmgr2,arch\", /arm-io/pmgr-child does not count", &Machine, RETURN_SUCCESS, M4_PMGR_ANS);

    // The die
    Ans                  = mAnsOfM4MacBookPro;
    Ans.ClockGates       = &DieOne;
    Ans.ClockGatesLength = sizeof(DieOne);
    CheckM4AnsPmgr("\"clock-gates\" names die 1", &Ans, RETURN_SUCCESS, M4_PMGR_ANS + DIE_1);
    Ans.ClockGates       = DieOneTwice;
    Ans.ClockGatesLength = sizeof(DieOneTwice);
    CheckM4AnsPmgr("two values in \"clock-gates\": the first names the die", &Ans, RETURN_SUCCESS, M4_PMGR_ANS + DIE_1);
    Ans.ClockGatesLength = 6;
    CheckM4AnsPmgr("\"clock-gates\" not a whole number of values", &Ans, RETURN_INVALID_PARAMETER, 0);
    Ans.Omit             = OMIT_ANS_CLOCK_GATES;
    CheckM4AnsPmgr("no \"clock-gates\": die 0 from the NVMe registers", &Ans, RETURN_SUCCESS, M4_PMGR_ANS);
    // Entry 9 where no range holds it, bits 37-38 = 1
    memcpy(AnsReg, mAnsRegOfM4MacBookPro, sizeof(AnsReg));
    AnsReg[9][0]   = 0x20c5cc0000ULL;
    Ans.Reg        = (CONST UINT64 (*)[2])AnsReg;
    CheckM4AnsPmgr("no \"clock-gates\", NVMe registers at 0x20_c5cc_0000: die 1", &Ans, RETURN_SUCCESS, M4_PMGR_ANS + DIE_1);
    Ans.Omit             = 0;
    Ans.ClockGates       = NULL;
    Ans.ClockGatesLength = 0;
    CheckM4AnsPmgr("an empty \"clock-gates\" counts as none", &Ans, RETURN_SUCCESS, M4_PMGR_ANS + DIE_1);

    //
    // Missing
    //
    Machine      = mM4MacBookProWithAns;
    Machine.Pmgr = NULL;
    CheckPmgr("no /arm-io/pmgr", &Machine, RETURN_NOT_FOUND, 0);
    Pmgr      = mPmgrOfM4MacBookPro;
    Pmgr.Omit = OMIT_PMGR_REG;
    CheckM4Pmgr("no \"reg\"", &Pmgr, RETURN_NOT_FOUND, 0);
    Pmgr.Omit = OMIT_PMGR_GROUPS;
    CheckM4Pmgr("neither \"ps-regs\" nor \"ps-groups\"", &Pmgr, RETURN_NOT_FOUND, 0);
    Pmgr.Omit = OMIT_PMGR_DEVICES;
    CheckM4Pmgr("no \"devices\"", &Pmgr, RETURN_NOT_FOUND, 0);
    Pmgr.Omit          = 0;
    Pmgr.DevicesLength = 0;
    CheckM4Pmgr("an empty \"devices\"", &Pmgr, RETURN_NOT_FOUND, 0);
    CheckM4Devices("no entry named ANS or ANS2; ANS-V does not count", DevicesWith(Devices, PMGR_DEVICE_INDEX_ANS, "ANX", 0x20), sizeof(Devices), RETURN_NOT_FOUND, 0);
    CheckM4Devices("the entry named ANS virtual", DevicesWith(Devices, PMGR_DEVICE_INDEX_ANS, "ANS", 0x30), sizeof(Devices), RETURN_NOT_FOUND, 0);
    CheckM4Devices("an entry named ANS1 is not ANS", DevicesWith(Devices, PMGR_DEVICE_INDEX_ANS, "ANS1", 0x20), sizeof(Devices), RETURN_NOT_FOUND, 0);

    //
    // Cannot be used
    //
    CheckM4Devices("entries named ANS and ANS2: which is the controller is not known", DevicesWith(Devices, 3, "ANS2", 0x00), sizeof(Devices), RETURN_UNSUPPORTED, 0);
    CheckM4Devices("\"devices\" not a whole number of entries", mPmgrDevicesOfM4MacBookPro, sizeof(mPmgrDevicesOfM4MacBookPro) - 4, RETURN_INVALID_PARAMETER, 0);
    Pmgr              = mPmgrOfM4MacBookPro;
    Pmgr.GroupsLength = sizeof(mPmgrPsRegsOfM4MacBookPro) - 4;
    CheckM4Pmgr("\"ps-regs\" not a whole number of entries", &Pmgr, RETURN_INVALID_PARAMETER, 0);
    Pmgr.GroupsLength = 5 * 12;
    CheckM4Pmgr("\"ps-regs\" without the entry's group 5", &Pmgr, RETURN_INVALID_PARAMETER, 0);
    Pmgr.GroupsLength = 6 * 12;
    CheckM4Pmgr("\"ps-regs\" up to group 5 is enough", &Pmgr, RETURN_SUCCESS, M4_PMGR_ANS);
    memcpy(Groups, mPmgrPsRegsOfM4MacBookPro, sizeof(Groups));
    Groups[5 * 3] = 2;
    Pmgr          = mPmgrOfM4MacBookPro;
    Pmgr.Groups   = Groups;
    CheckM4Pmgr("group 5 names \"reg\" entry 2, past the last", &Pmgr, RETURN_INVALID_PARAMETER, 0);

    memcpy(Reg, mPmgrRegOfM4MacBookPro, sizeof(Reg));
    Reg[0][1]     = 0x53b;
    Pmgr          = mPmgrOfM4MacBookPro;
    Pmgr.Reg      = (CONST UINT64 (*)[2])Reg;
    CheckM4Pmgr("\"reg\" entry 0 ends before the last byte of the register", &Pmgr, RETURN_INVALID_PARAMETER, 0);
    Reg[0][1]     = 0x53c;
    CheckM4Pmgr("\"reg\" entry 0 ends with the register", &Pmgr, RETURN_SUCCESS, M4_PMGR_ANS);
    Reg[0][0]     = 0;
    Reg[0][1]     = 0;
    CheckM4Pmgr("\"reg\" entry 0 empty", &Pmgr, RETURN_INVALID_PARAMETER, 0);

    // At the top of the address space: no range holds it, so it stays where it is
    Pmgr           = mPmgrOfM4MacBookPro;
    Pmgr.Reg       = TopReg;
    Pmgr.RegLength = sizeof(TopReg);
    CheckM4Pmgr("a register 0xac8 below the top of the address space", &Pmgr, RETURN_SUCCESS, 0xfffffffffffff538ULL);
    Machine           = mM4MacBookProWithAns;
    Machine.Pmgr      = &Pmgr;
    Ans               = mAnsOfM4MacBookPro;
    Ans.ClockGates       = &DieOne;
    Ans.ClockGatesLength = sizeof(DieOne);
    Machine.Ans       = &Ans;
    CheckPmgr("the same on die 1 wraps around the address space", &Machine, RETURN_INVALID_PARAMETER, 0);
}

int main(void)
{
    TEST_ANS     Ans;
    TEST_MACHINE Machine;
    APPLE_ANS    Expected;
    UINT64       Sart[1][2] = { { 0x285c50000ULL, 0 } };
    UINT32       Interrupts[6];

    //
    // Read
    //
    Check("M4 MacBook Pro: NVMe 0x4_c5cc_0000+0x60000, coprocessor 0x4_8160_0000+0x88000, SART 0x4_85c5_0000+0xc000, AIC 1018 and 1006",
          &mM4MacBookProWithAns, RETURN_SUCCESS, &mM4Ans);

    // The node before it without a name, as m1n1 hands the tree over without a host
    Machine      = mM4MacBookProWithAns;
    Machine.Uart = TestUartNameCleared;
    Check("the same with the name of uart0 cleared", &Machine, RETURN_SUCCESS, &mM4Ans);

    Ans                = mAnsOfM4MacBookPro;
    Ans.SecureBar      = FALSE;
    Expected           = mM4Ans;
    Expected.Nvme.Base = 0x485cc0000ULL;
    Expected.SecureBar = FALSE;
    CheckM4With("M4 MacBook Pro without \"nvme-secure-bar\": the NVMe window is entry 3", &Ans, RETURN_SUCCESS, &Expected);

    Machine        = mM4MacBookProWithAns;
    Machine.Ranges = mOtherRanges;
    Machine.RangeCount = ARRAY_SIZE(mOtherRanges);
    Machine.Ans    = &mOtherAns;
    Check("four \"reg\" entries, two ranges, six lines with the controller's last", &Machine, RETURN_SUCCESS, &mOtherExpected);

    Machine.Ranges     = NULL;
    Machine.RangeCount = 0;
    Expected           = mOtherExpected;
    Expected.Nvme.Base        = 0x093cc0000ULL;
    Expected.Coprocessor.Base = 0x100400000ULL;
    Expected.Sart.Base        = 0x093c50000ULL;
    Check("empty \"ranges\": the addresses stay as they are", &Machine, RETURN_SUCCESS, &Expected);
    {
        // Address 0 and size 0: the one empty entry that does not also wrap around
        UINT64   Reg[ARRAY_SIZE(mOtherReg)][2];
        TEST_ANS Other = mOtherAns;

        memcpy(Reg, mOtherReg, sizeof(Reg));
        Reg[0][0]   = 0;
        Reg[0][1]   = 0;
        Other.Reg   = (CONST UINT64 (*)[2])Reg;
        Machine.Ans = &Other;
        Check("an entry 0 at address 0 of size 0", &Machine, RETURN_INVALID_PARAMETER, NULL);
    }

    Expected = mM4Ans;
    Expected.Coprocessor.Base = 0x900000000ULL;
    CheckM4Reg("a coprocessor entry no range holds stays as it is", 0, 0x900000000ULL, 0x88000, RETURN_SUCCESS, &Expected);
    Expected.Coprocessor.Base = 0xfffffffffff78000ULL;
    CheckM4Reg("a coprocessor entry that ends at the last byte of the address space", 0, 0xfffffffffff78000ULL, 0x88000, RETURN_SUCCESS, &Expected);

    //
    // Missing
    //
    Check("M4 MacBook Pro without a disk controller", &mM4MacBookPro, RETURN_NOT_FOUND, NULL);
    CheckM4Without("no /arm-io/ans; /ans does not count", OMIT_ANS, RETURN_NOT_FOUND);
    CheckM4Without("no /arm-io/sart-ans", OMIT_SART, RETURN_NOT_FOUND);
    CheckM4Without("/arm-io/ans without \"reg\"; its child's does not count", OMIT_ANS_REG, RETURN_NOT_FOUND);
    CheckM4Without("/arm-io/sart-ans without \"reg\"", OMIT_SART_REG, RETURN_NOT_FOUND);
    CheckM4Without("no \"interrupts\"", OMIT_ANS_INTERRUPTS, RETURN_NOT_FOUND);
    CheckM4Without("no \"nvme-interrupt-idx\"", OMIT_ANS_INTERRUPT_IDX, RETURN_NOT_FOUND);

    //
    // Cannot be used
    //
    Ans           = mAnsOfM4MacBookPro;
    Ans.RegLength = 9 * 16;
    CheckM4With("\"nvme-secure-bar\" and nine \"reg\" entries: no entry 9", &Ans, RETURN_INVALID_PARAMETER, NULL);
    Ans.RegLength = 10 * 16 - 8;
    CheckM4With("\"reg\" ends in the middle of entry 9", &Ans, RETURN_INVALID_PARAMETER, NULL);
    Ans.RegLength = 10 * 16;
    CheckM4With("ten \"reg\" entries are enough", &Ans, RETURN_SUCCESS, &mM4Ans);
    Ans.SecureBar = FALSE;
    Ans.RegLength = 3 * 16;
    CheckM4With("no \"nvme-secure-bar\" and three \"reg\" entries: no entry 3", &Ans, RETURN_INVALID_PARAMETER, NULL);
    Ans.RegLength = 0;
    CheckM4With("an empty \"reg\"", &Ans, RETURN_INVALID_PARAMETER, NULL);

    Ans               = mAnsOfM4MacBookPro;
    Ans.SartRegLength = 0;
    CheckM4With("an empty \"reg\" of sart-ans", &Ans, RETURN_INVALID_PARAMETER, NULL);
    Ans.SartReg       = (CONST UINT64 (*)[2])Sart;
    Ans.SartRegLength = sizeof(Sart);
    CheckM4With("a SART of size 0", &Ans, RETURN_INVALID_PARAMETER, NULL);

    CheckM4Reg("an entry 3 of size 0: the NVMe window would be empty", 3, 0x285cc0000ULL, 0, RETURN_INVALID_PARAMETER, NULL);
    CheckM4Reg("an entry 0 of size 0", 0, 0x281600000ULL, 0, RETURN_INVALID_PARAMETER, NULL);
    CheckM4Reg("an entry 0 that wraps around the address space", 0, 0xfffffffffff80000ULL, 0x88000, RETURN_INVALID_PARAMETER, NULL);
    CheckM4Reg("an NVMe window that wraps around: entry 9 near the top, entry 3 long", 9, 0xfffffffffffd0000ULL, 0x10000, RETURN_INVALID_PARAMETER, NULL);

    // The M4's lines and room for two bytes more
    memset(Interrupts, 0, sizeof(Interrupts));
    memcpy(Interrupts, mAnsInterruptsOfM4MacBookPro, sizeof(mAnsInterruptsOfM4MacBookPro));
    Ans                  = mAnsOfM4MacBookPro;
    Ans.Interrupts       = Interrupts;
    Ans.NvmeInterruptIdx = 3;
    CheckM4With("\"nvme-interrupt-idx\" names a mailbox line", &Ans, RETURN_INVALID_PARAMETER, NULL);
    Ans.NvmeInterruptIdx = 5;
    CheckM4With("\"nvme-interrupt-idx\" past the last line", &Ans, RETURN_INVALID_PARAMETER, NULL);
    Ans.NvmeInterruptIdx = 4;
    Ans.InterruptsLength = 4 * 4;
    CheckM4With("only the four mailbox lines", &Ans, RETURN_INVALID_PARAMETER, NULL);
    Ans.InterruptsLength = 5 * 4 - 2;
    CheckM4With("\"interrupts\" not a whole number of values", &Ans, RETURN_INVALID_PARAMETER, NULL);
    Ans.InterruptsLength = 5 * 4 + 2;
    CheckM4With("\"interrupts\" with two bytes too many", &Ans, RETURN_INVALID_PARAMETER, NULL);
    Ans.InterruptsLength = 0;
    CheckM4With("an empty \"interrupts\"", &Ans, RETURN_INVALID_PARAMETER, NULL);

    // The lines are taken by position, not by number
    Interrupts[0] = 5; Interrupts[1] = 6; Interrupts[2] = 7; Interrupts[3] = 8; Interrupts[4] = 9;
    Ans                  = mAnsOfM4MacBookPro;
    Ans.Interrupts       = Interrupts;
    Expected             = mM4Ans;
    Expected.NvmeAic     = 9;
    Expected.MailboxAic  = 7;
    CheckM4With("lines 5 to 9: the controller's is 9, the mailbox's 7", &Ans, RETURN_SUCCESS, &Expected);

    CheckSartVersions();
    CheckPmgrs();

    return Verdict();
}
