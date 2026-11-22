/** @file
  Host test of AppleGetAns() in Library/AppleDTLib: what the firmware reads about
  the internal disk controller, /arm-io/ans and /arm-io/sart-ans.

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
// the controller's last.
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

STATIC CONST TEST_ANS mOtherAns = {
    mOtherReg, sizeof(mOtherReg),
    mOtherInterrupts, sizeof(mOtherInterrupts),
    5, FALSE,
    mOtherSartReg, sizeof(mOtherSartReg),
    0
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

    return Verdict();
}
